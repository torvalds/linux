/*
 * Linux Wireless Extensions support
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
 * $Id: wl_iw.c,v 1.132.2.17.8.1 2011/02/05 01:45:40 Exp $
 */
#include <wlioctl.h>

#include <typedefs.h>
#include <linuxver.h>
#include <osl.h>

#include <bcmutils.h>
#include <bcmendian.h>
#include <proto/ethernet.h>

#include <linux/if_arp.h>
#include <asm/uaccess.h>

#include <dngl_stats.h>
#include <dhd.h>
#include <dhdioctl.h>

typedef void wlc_info_t;
typedef void wl_info_t;
typedef const struct si_pub  si_t;
#include <wlioctl.h>

#include <proto/ethernet.h>
#include <dngl_stats.h>
#include <dhd.h>
#define WL_ERROR(x) printf x
#define WL_TRACE(x)
#define WL_ASSOC(x)
#define WL_INFORM(x)
#define WL_WSEC(x)
#define WL_SCAN(x)

#include <wl_iw.h>



#define IW_WSEC_ENABLED(wsec)	((wsec) & (WEP_ENABLED | TKIP_ENABLED | AES_ENABLED))

#include <linux/rtnetlink.h>

#define WL_IW_USE_ISCAN  1
#define ENABLE_ACTIVE_PASSIVE_SCAN_SUPPRESS  1

#ifdef OEM_CHROMIUMOS
bool g_set_essid_before_scan = TRUE;
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)) && 1
	struct mutex  g_wl_ss_scan_lock; /* lock/unlock for ISCAN cache settings */
#endif 

#if defined(SOFTAP)
#define WL_SOFTAP(x) printk x
static struct net_device *priv_dev;
static bool 	ap_cfg_running = FALSE;
static bool 	ap_fw_loaded = FALSE;
struct net_device *ap_net_dev = NULL;
struct semaphore  ap_eth_sema;
static int wl_iw_set_ap_security(struct net_device *dev, struct ap_profile *ap);
static int wl_iw_softap_deassoc_stations(struct net_device *dev);
#endif /* SOFTAP */

#define WL_IW_IOCTL_CALL(func_call) \
	do {				\
		func_call;		\
	} while (0)

#define RETURN_IF_EXTRA_NULL(extra) \
	if (!extra) { \
		WL_ERROR(("%s: error : extra is null pointer\n", __FUNCTION__)); \
		return -EINVAL; \
	}

static int		g_onoff = G_WLAN_SET_ON;
wl_iw_extra_params_t	g_wl_iw_params;


#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)) && 1
/* 
 * wl_start_lock to replace MUTEX_LOCK[dhd.h]/dhd_pub_t::wl_start_stop_lock[dhd.h]
 * wl_cache_lock  to replace MUTEX_LOCK_WL_SCAN_SET[dhd.h]/g_wl_ss_scan_lock[wl_iw.c]
 */
static struct mutex	wl_start_lock;
static struct mutex	wl_cache_lock;
static struct mutex	wl_softap_lock;

#define DHD_OS_MUTEX_INIT(a) mutex_init(a)
#define DHD_OS_MUTEX_LOCK(a) mutex_lock(a)
#define DHD_OS_MUTEX_UNLOCK(a) mutex_unlock(a)

#else

#define DHD_OS_MUTEX_INIT(a)
#define DHD_OS_MUTEX_LOCK(a)
#define DHD_OS_MUTEX_UNLOCK(a)

#endif 

#include <bcmsdbus.h>
extern void dhd_customer_gpio_wlan_ctrl(int onoff);
extern uint dhd_dev_reset(struct net_device *dev, uint8 flag);
extern void dhd_dev_init_ioctl(struct net_device *dev);

uint wl_msg_level = WL_ERROR_VAL;

#define MAX_WLIW_IOCTL_LEN 1024

/* IOCTL swapping mode for Big Endian host with Little Endian dongle.  Default to off */
#define htod32(i) i
#define htod16(i) i
#define dtoh32(i) i
#define dtoh16(i) i
#define htodchanspec(i) i
#define dtohchanspec(i) i

#ifdef CONFIG_WIRELESS_EXT

extern struct iw_statistics *dhd_get_wireless_stats(struct net_device *dev);
extern int dhd_wait_pend8021x(struct net_device *dev);
#endif /* CONFIG_WIRELESS_EXT */

#if WIRELESS_EXT < 19
#define IW_IOCTL_IDX(cmd)	((cmd) - SIOCIWFIRST)
#define IW_EVENT_IDX(cmd)	((cmd) - IWEVFIRST)
#endif /* WIRELESS_EXT < 19 */

static void *g_scan = NULL;
static volatile uint g_scan_specified_ssid;	/* current scan type flag, specific or broadcast */
static wlc_ssid_t g_specific_ssid;		/* cached specific ssid request */
/* caching current ssid */
static wlc_ssid_t g_ssid;

static wl_iw_ss_cache_ctrl_t g_ss_cache_ctrl;	/* spec scan cache controller instance */
static volatile uint g_first_broadcast_scan;	/* forcing first scan as always broadcast state  */
static volatile uint g_first_counter_scans;
#define MAX_ALLOWED_BLOCK_SCAN_FROM_FIRST_SCAN 3

//static wlc_ssid_t g_ssids[WL_SCAN_PARAMS_SSID_MAX];  /* Keep track of cached ssid */


#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0))
#define DAEMONIZE(a) daemonize(a); \
	allow_signal(SIGKILL); \
	allow_signal(SIGTERM);
#else /* Linux 2.4 (w/o preemption patch) */
#define RAISE_RX_SOFTIRQ() \
	cpu_raise_softirq(smp_processor_id(), NET_RX_SOFTIRQ)
#define DAEMONIZE(a) daemonize(); \
	do { if (a) \
		strncpy(current->comm, a, MIN(sizeof(current->comm), (strlen(a) + 1))); \
	} while (0);
#endif /* LINUX_VERSION_CODE  */

#if defined(WL_IW_USE_ISCAN)
#if  !defined(CSCAN)
static void wl_iw_free_ss_cache(void);
static int   wl_iw_run_ss_cache_timer(int kick_off);
#endif /* !defined(CSCAN) */
int  wl_iw_iscan_set_scan_broadcast_prep(struct net_device *dev, uint flag);
static int dev_wlc_bufvar_set(struct net_device *dev, char *name, char *buf, int len);
#define ISCAN_STATE_IDLE   0
#define ISCAN_STATE_SCANING 1

/* the buf lengh can be WLC_IOCTL_MAXLEN (8K) to reduce iteration */
#define WLC_IW_ISCAN_MAXLEN   2048
typedef struct iscan_buf {
	struct iscan_buf * next;
	char   iscan_buf[WLC_IW_ISCAN_MAXLEN];
} iscan_buf_t;

typedef struct iscan_info {
	struct net_device *dev;
	struct timer_list timer;
	uint32 timer_ms;
	uint32 timer_on;
	int    iscan_state;
	iscan_buf_t * list_hdr;
	iscan_buf_t * list_cur;

	/* Thread to work on iscan */
	long sysioc_pid;
	struct semaphore sysioc_sem;
	struct completion sysioc_exited;

	uint32 scan_flag;	/* for active/passive scan */
#if defined CSCAN
	char ioctlbuf[WLC_IOCTL_MEDLEN];
#else
	char ioctlbuf[WLC_IOCTL_SMLEN];
#endif /* CSCAN */
	/* pointer to extra iscan params from wl_scan_params */
	wl_iscan_params_t *iscan_ex_params_p;
	int iscan_ex_param_size;
} iscan_info_t;

#define  COEX_DHCP 1 /* enable bt coex during dhcp */
#ifdef COEX_DHCP
static void wl_iw_bt_flag_set(struct net_device *dev, bool set);
static void wl_iw_bt_release(void);

typedef enum bt_coex_status {
	BT_DHCP_IDLE = 0,
	BT_DHCP_START,
	BT_DHCP_OPPORTUNITY_WINDOW,
	BT_DHCP_FLAG_FORCE_TIMEOUT
} coex_status_t;
#define BT_DHCP_OPPORTUNITY_WINDOW_TIEM	2500	/* msec to get DHCP address */
#define BT_DHCP_FLAG_FORCE_TIME				5500 	/* msec to force BT flag  max */

typedef struct bt_info {
	struct net_device *dev;
	struct timer_list timer;
	uint32 timer_ms;
	uint32 timer_on;
	int	bt_state;

	/* Thread to work on bt dhcp */
	long bt_pid;
	struct semaphore bt_sem;
	struct completion bt_exited;
} bt_info_t;

bt_info_t *g_bt = NULL;
static void wl_iw_bt_timerfunc(ulong data);
#endif /* COEX_DHCP */
iscan_info_t *g_iscan = NULL;
void dhd_print_buf(void *pbuf, int len, int bytes_per_line);
static void wl_iw_timerfunc(ulong data);
static void wl_iw_set_event_mask(struct net_device *dev);
static int
wl_iw_iscan(iscan_info_t *iscan, wlc_ssid_t *ssid, uint16 action);
#endif /* WL_IW_USE_ISCAN */

static int
wl_iw_set_scan(
	struct net_device *dev,
	struct iw_request_info *info,
	union iwreq_data *wrqu,
	char *extra
);

static int
wl_iw_get_scan(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_point *dwrq,
	char *extra
);

static uint
wl_iw_get_scan_prep(
	wl_scan_results_t *list,
	struct iw_request_info *info,
	char *extra,
	short max_size
);


static void
swap_key_from_BE(
	wl_wsec_key_t *key
)
{
	key->index = htod32(key->index);
	key->len = htod32(key->len);
	key->algo = htod32(key->algo);
	key->flags = htod32(key->flags);
	key->rxiv.hi = htod32(key->rxiv.hi);
	key->rxiv.lo = htod16(key->rxiv.lo);
	key->iv_initialized = htod32(key->iv_initialized);
}

static void
swap_key_to_BE(
	wl_wsec_key_t *key
)
{
	key->index = dtoh32(key->index);
	key->len = dtoh32(key->len);
	key->algo = dtoh32(key->algo);
	key->flags = dtoh32(key->flags);
	key->rxiv.hi = dtoh32(key->rxiv.hi);
	key->rxiv.lo = dtoh16(key->rxiv.lo);
	key->iv_initialized = dtoh32(key->iv_initialized);
}

static int
dev_wlc_ioctl(
	struct net_device *dev,
	int cmd,
	void *arg,
	int len
)
{
	struct ifreq ifr;
	wl_ioctl_t ioc;
	mm_segment_t fs;
	int ret = -EINVAL;

	if (!dev) {
		WL_ERROR(("%s: dev is null\n", __FUNCTION__));
		return ret;
	}

	net_os_wake_lock(dev);

	WL_INFORM(("%s, PID:%x: send Local IOCTL -> dhd: cmd:0x%x, buf:%p, len:%d ,\n",
		__FUNCTION__, current->pid, cmd, arg, len));

	if (g_onoff == G_WLAN_SET_ON) {
		memset(&ioc, 0, sizeof(ioc));
		ioc.cmd = cmd;
		ioc.buf = arg;
		ioc.len = len;

		strcpy(ifr.ifr_name, dev->name);
		ifr.ifr_data = (caddr_t) &ioc;

		/* Must be up for virtually all useful ioctls */
		ret = dev_open(dev);
		if (ret) {
			WL_ERROR(("%s: Error dev_open: %d\n", __func__, ret));
			net_os_wake_unlock(dev);
			return ret;
		}

		fs = get_fs();
		set_fs(get_ds());
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 31)
		ret = dev->do_ioctl(dev, &ifr, SIOCDEVPRIVATE);
#else
		ret = dev->netdev_ops->ndo_do_ioctl(dev, &ifr, SIOCDEVPRIVATE);
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 31) */
		set_fs(fs);
	}
	else {
		WL_TRACE(("%s: call after driver stop : ignored\n", __FUNCTION__));
	}

	net_os_wake_unlock(dev);

	return ret;
}

/*
get named driver variable to uint register value and return error indication
calling example: dev_wlc_intvar_get_reg(dev, "btc_params",66, &reg_value)
*/
static int
dev_wlc_intvar_get_reg(
	struct net_device *dev,
	char *name,
	uint  reg,
	int *retval)
{
	union {
		char buf[WLC_IOCTL_SMLEN];
		int val;
	} var;
	int error;

	uint len;
	len = bcm_mkiovar(name, (char *)(&reg), sizeof(reg), (char *)(&var), sizeof(var.buf));
	ASSERT(len);
	error = dev_wlc_ioctl(dev, WLC_GET_VAR, (void *)&var, len);

	*retval = dtoh32(var.val);
	return (error);
}

/*
get named driver variable to uint register value and return error indication
calling example: dev_wlc_intvar_set_reg(dev, "btc_params",66, value)
*/
static int
dev_wlc_intvar_set_reg(
	struct net_device *dev,
	char *name,
	char *addr,
	char * val)
{
	char reg_addr[8];

	memset(reg_addr, 0, sizeof(reg_addr));
	memcpy((char *)&reg_addr[0], (char *)addr, 4);
	memcpy((char *)&reg_addr[4], (char *)val, 4);

	return (dev_wlc_bufvar_set(dev, name,  (char *)&reg_addr[0], sizeof(reg_addr)));
}


/*
set named driver variable to int value and return error indication
calling example: dev_wlc_intvar_set(dev, "arate", rate)
*/

static int
dev_wlc_intvar_set(
	struct net_device *dev,
	char *name,
	int val)
{
	char buf[WLC_IOCTL_SMLEN];
	uint len;

	val = htod32(val);
	len = bcm_mkiovar(name, (char *)(&val), sizeof(val), buf, sizeof(buf));
	ASSERT(len);

	return (dev_wlc_ioctl(dev, WLC_SET_VAR, buf, len));
}

#if defined(WL_IW_USE_ISCAN)
static int
dev_iw_iovar_setbuf(
	struct net_device *dev,
	char *iovar,
	void *param,
	int paramlen,
	void *bufptr,
	int buflen)
{
	int iolen;

	iolen = bcm_mkiovar(iovar, param, paramlen, bufptr, buflen);
	ASSERT(iolen);

	if (iolen == 0)
		return 0;

	return (dev_wlc_ioctl(dev, WLC_SET_VAR, bufptr, iolen));
}

static int
dev_iw_iovar_getbuf(
	struct net_device *dev,
	char *iovar,
	void *param,
	int paramlen,
	void *bufptr,
	int buflen)
{
	int iolen;

	iolen = bcm_mkiovar(iovar, param, paramlen, bufptr, buflen);
	ASSERT(iolen);

	return (dev_wlc_ioctl(dev, WLC_GET_VAR, bufptr, buflen));
}
#endif /* #ifdef BCMDONGLEHOST */


#if WIRELESS_EXT > 17
static int
dev_wlc_bufvar_set(
	struct net_device *dev,
	char *name,
	char *buf, int len)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 31)
	char ioctlbuf[MAX_WLIW_IOCTL_LEN];
#else
	static char ioctlbuf[MAX_WLIW_IOCTL_LEN];
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 31) */
	uint buflen;

	buflen = bcm_mkiovar(name, buf, len, ioctlbuf, sizeof(ioctlbuf));
	ASSERT(buflen);

	return (dev_wlc_ioctl(dev, WLC_SET_VAR, ioctlbuf, buflen));
}
#endif /* WIRELESS_EXT > 17 */
/*
get named driver variable to int value and return error indication
calling example: dev_wlc_intvar_get(dev, "arate", &rate)
*/

static int
dev_wlc_bufvar_get(
	struct net_device *dev,
	char *name,
	char *buf, int buflen)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 31)
	char ioctlbuf[MAX_WLIW_IOCTL_LEN];
#else
	static char ioctlbuf[MAX_WLIW_IOCTL_LEN];
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 31) */
	int error;
	uint len;

	len = bcm_mkiovar(name, NULL, 0, ioctlbuf, sizeof(ioctlbuf));
	ASSERT(len);
	error = dev_wlc_ioctl(dev, WLC_GET_VAR, (void *)ioctlbuf, MAX_WLIW_IOCTL_LEN);
	if (!error)
		bcopy(ioctlbuf, buf, buflen);

	return (error);
}

/*
get named driver variable to int value and return error indication
calling example: dev_wlc_intvar_get(dev, "arate", &rate)
*/

static int
dev_wlc_intvar_get(
	struct net_device *dev,
	char *name,
	int *retval)
{
	union {
		char buf[WLC_IOCTL_SMLEN];
		int val;
	} var;
	int error;

	uint len;
	uint data_null;

	len = bcm_mkiovar(name, (char *)(&data_null), 0, (char *)(&var), sizeof(var.buf));
	ASSERT(len);
	error = dev_wlc_ioctl(dev, WLC_GET_VAR, (void *)&var, len);

	*retval = dtoh32(var.val);

	return (error);
}

/* Maintain backward compatibility */
#if WIRELESS_EXT > 12
static int
wl_iw_set_active_scan(
	struct net_device *dev,
	struct iw_request_info *info,
	union iwreq_data *wrqu,
	char *extra
)
{
	int as = 0;
	int error = 0;
	char *p = extra;

#if defined(WL_IW_USE_ISCAN)
	if (g_iscan->iscan_state == ISCAN_STATE_IDLE)
#endif /* WL_IW_USE_ISCAN */
		error = dev_wlc_ioctl(dev, WLC_SET_PASSIVE_SCAN, &as, sizeof(as));
#if defined(WL_IW_USE_ISCAN)
	else
		g_iscan->scan_flag = as;
#endif /* WL_IW_USE_ISCAN */
	p += snprintf(p, MAX_WX_STRING, "OK");

	wrqu->data.length = p - extra + 1;
	return error;
}

static int
wl_iw_set_passive_scan(
	struct net_device *dev,
	struct iw_request_info *info,
	union iwreq_data *wrqu,
	char *extra
)
{
	int ps = 1;
	int error = 0;
	char *p = extra;

#if defined(WL_IW_USE_ISCAN)
	if (g_iscan->iscan_state == ISCAN_STATE_IDLE) {
#endif /* WL_IW_USE_ISCAN */

		 /*
		 Specific SSID scan required active scan
		 so have to ignore passive scan during specific SSID scan request
		*/
		if (g_scan_specified_ssid == 0) {
			error = dev_wlc_ioctl(dev, WLC_SET_PASSIVE_SCAN, &ps, sizeof(ps));
		}
#if defined(WL_IW_USE_ISCAN)
	}
	else
		g_iscan->scan_flag = ps;
#endif /* WL_IW_USE_ISCAN */

	p += snprintf(p, MAX_WX_STRING, "OK");

	wrqu->data.length = p - extra + 1;
	return error;
}

static int
wl_iw_get_macaddr(
	struct net_device *dev,
	struct iw_request_info *info,
	union iwreq_data *wrqu,
	char *extra
)
{
	int error;
	char buf[128];
	struct ether_addr *id;
	char *p = extra;

	/* Get the device MAC address */
	strcpy(buf, "cur_etheraddr");
	error = dev_wlc_ioctl(dev, WLC_GET_VAR, buf, sizeof(buf));
	id = (struct ether_addr *) buf;
	p += snprintf(p, MAX_WX_STRING, "Macaddr = %02X:%02X:%02X:%02X:%02X:%02X\n",
		id->octet[0], id->octet[1], id->octet[2],
		id->octet[3], id->octet[4], id->octet[5]);
	wrqu->data.length = p - extra + 1;

	return error;
}


/* Private IOCTL should provide country code as defined in ISO 3166-1 */
static int
wl_iw_set_country(
	struct net_device *dev,
	struct iw_request_info *info,
	union iwreq_data *wrqu,
	char *extra
)
{
	char country_code[WLC_CNTRY_BUF_SZ];
	int error = 0;
	char *p = extra;
	int country_offset;
	int country_code_size;

	memset(country_code, 0, sizeof(country_code));

	/* Search for the country code */
	country_offset = strcspn(extra, " ");
	country_code_size = strlen(extra) - country_offset;

	/* Get country string */
	if (country_offset != 0) {
		strncpy(country_code, extra + country_offset +1,
			MIN(country_code_size, sizeof(country_code)));

		/*
		Trying to setup new country code
		Note that if country code was not proper
		Dongle will continue to use default country code
		*/
		if ((error = dev_wlc_ioctl(dev, WLC_SET_COUNTRY,
			&country_code, sizeof(country_code))) >= 0) {
			p += snprintf(p, MAX_WX_STRING, "OK");
			WL_TRACE(("%s: set country %s OK\n", __FUNCTION__, country_code));
			goto exit;
		}
	}

	WL_ERROR(("%s: set country %s failed code %d\n", __FUNCTION__, country_code, error));
	p += snprintf(p, MAX_WX_STRING, "FAIL");

exit:
	wrqu->data.length = p - extra + 1;
	return error;
}
/*
*    DHCP  session off/on
*/
#ifdef CONFIG_MACH_MAHIMAHI
static int
wl_iw_set_power_mode(
	struct net_device *dev,
	struct iw_request_info *info,
	union iwreq_data *wrqu,
	char *extra
)
{
	int error = 0;
	char *p = extra;
	static int  pm = PM_FAST;
	int  pm_local = PM_OFF;
	char powermode_val = 0;

	strncpy((char *)&powermode_val, extra + strlen("POWERMODE") +1, 1);

	if (strnicmp((char *)&powermode_val, "1", strlen("1")) == 0) {

		WL_TRACE(("%s: DHCP session starts\n", __FUNCTION__));

		dev_wlc_ioctl(dev, WLC_GET_PM, &pm, sizeof(pm));
		dev_wlc_ioctl(dev, WLC_SET_PM, &pm_local, sizeof(pm_local));

		/* Disable packet filtering if necessary */
		net_os_set_packet_filter(dev, 0);

	} else if (strnicmp((char *)&powermode_val, "0", strlen("0")) == 0) {

		WL_TRACE(("%s: DHCP session done\n", __FUNCTION__));

		dev_wlc_ioctl(dev, WLC_SET_PM, &pm, sizeof(pm));

		/* Enable packet filtering if was turned off */
		net_os_set_packet_filter(dev, 1);

	} else {
		WL_ERROR(("Unkwown yet power setting, ignored\n"));
	}

	p += snprintf(p, MAX_WX_STRING, "OK");

	wrqu->data.length = p - extra + 1;

	return error;
}
#endif /*  CONFIG_MACH_MAHIMAHI */

#ifdef CONFIG_MACH_MAHIMAHI
static int
wl_iw_get_power_mode(
	struct net_device *dev,
	struct iw_request_info *info,
	union iwreq_data *wrqu,
	char *extra
)
{
	int error;
	char *p = extra;
	int pm_local = PM_FAST;

	error = dev_wlc_ioctl(dev, WLC_GET_PM, &pm_local, sizeof(pm_local));
	if (!error) {
		WL_TRACE(("%s: Powermode = %d\n", __func__, pm_local));
		if (pm_local == PM_OFF)
			pm_local = 1; /* Active */
		else
			pm_local = 0; /* Auto */
		p += snprintf(p, MAX_WX_STRING, "powermode = %d", pm_local);
	}
	else {
		WL_TRACE(("%s: Error = %d\n", __func__, error));
		p += snprintf(p, MAX_WX_STRING, "FAIL");
	}
	wrqu->data.length = p - extra + 1;
	return error;
}
#endif /* CONFIG_MACH_MAHIMAHI */

static int
wl_iw_set_btcoex_dhcp(
	struct net_device *dev,
	struct iw_request_info *info,
	union iwreq_data *wrqu,
	char *extra
)
{
	int error = 0;
	char *p = extra;
#ifndef CONFIG_MACH_MAHIMAHI
	static int  pm = PM_FAST;
	int  pm_local = PM_OFF;
#endif
	char powermode_val = 0;
	char buf_reg66va_dhcp_on[8] = { 66, 00, 00, 00, 0x10, 0x27, 0x00, 0x00 };
	char buf_reg41va_dhcp_on[8] = { 41, 00, 00, 00, 0x33, 0x00, 0x00, 0x00 };
	char buf_reg68va_dhcp_on[8] = { 68, 00, 00, 00, 0x90, 0x01, 0x00, 0x00 };

	uint32 regaddr;
	static uint32 saved_reg66;
	static uint32 saved_reg41;
	static uint32 saved_reg68;
	static bool saved_status = FALSE;

#ifdef COEX_DHCP
	char buf_flag7_default[8] =   { 7, 00, 00, 00, 0x0, 0x00, 0x00, 0x00};
#ifndef CONFIG_MACH_MAHIMAHI
	uint32 temp1, temp2;
#endif /* CONFIG_MACH_MAHIMAHI */
#endif /* COEX_DHCP */

	/* Figure out powermode 1 or o command */
#ifdef  CONFIG_MACH_MAHIMAHI
	strncpy((char *)&powermode_val, extra + strlen("BTCOEXMODE") +1, 1);
#else
	strncpy((char *)&powermode_val, extra + strlen("POWERMODE") +1, 1);
#endif

	if (strnicmp((char *)&powermode_val, "1", strlen("1")) == 0) {

		WL_TRACE(("%s: DHCP session starts\n", __FUNCTION__));

		/* Retrieve and saved orig regs value */
		if ((saved_status == FALSE) &&
#ifndef CONFIG_MACH_MAHIMAHI
			(!dev_wlc_ioctl(dev, WLC_GET_PM, &pm, sizeof(pm))) &&
#endif
			(!dev_wlc_intvar_get_reg(dev, "btc_params", 66,  &saved_reg66)) &&
			(!dev_wlc_intvar_get_reg(dev, "btc_params", 41,  &saved_reg41)) &&
			(!dev_wlc_intvar_get_reg(dev, "btc_params", 68,  &saved_reg68)))   {
				saved_status = TRUE;
				WL_TRACE(("Saved 0x%x 0x%x 0x%x\n",
					saved_reg66, saved_reg41, saved_reg68));

				/* Disable PM mode during dhpc session */
#ifndef CONFIG_MACH_MAHIMAHI
				dev_wlc_ioctl(dev, WLC_SET_PM, &pm_local, sizeof(pm_local));
#endif

				/* Disable PM mode during dhpc session */
				dev_wlc_bufvar_set(dev, "btc_params",
				                   (char *)&buf_reg66va_dhcp_on[0],
				                   sizeof(buf_reg66va_dhcp_on));
				/* btc_params 41 0x33 */
				dev_wlc_bufvar_set(dev, "btc_params",
				                   (char *)&buf_reg41va_dhcp_on[0],
				                   sizeof(buf_reg41va_dhcp_on));
				/* btc_params 68 0x190 */
				dev_wlc_bufvar_set(dev, "btc_params",
				                   (char *)&buf_reg68va_dhcp_on[0],
				                   sizeof(buf_reg68va_dhcp_on));
#ifdef COEX_DHCP
				/* Start  BT timer only for SCO connection */
#ifndef CONFIG_MACH_MAHIMAHI
				if ((!dev_wlc_intvar_get_reg(dev, "btc_params", 12, &temp1)) &&
					(!dev_wlc_intvar_get_reg(dev, "btc_params", 13, &temp2)))
				{
					if ((temp1 != 0) && (temp2 != 0)) {
#endif
						g_bt->bt_state = BT_DHCP_START;
						g_bt->timer_on = 1;
						mod_timer(&g_bt->timer, g_bt->timer.expires);
						WL_TRACE(("%s enable BT DHCP Timer\n",
							__FUNCTION__));
#ifndef CONFIG_MACH_MAHIMAHI
					}
				}
#endif
#endif /* COEX_DHCP */
		}
		else if (saved_status == TRUE) {
			WL_ERROR(("%s was called w/o DHCP OFF. Continue\n", __FUNCTION__));
		}
	}
#ifdef  CONFIG_MACH_MAHIMAHI
	else if (strnicmp((char *)&powermode_val, "2", strlen("2")) == 0) {
#else
	else if (strnicmp((char *)&powermode_val, "0", strlen("0")) == 0) {
#endif

		WL_TRACE(("%s: DHCP session done\n", __FUNCTION__));

		/* Restoring PM mode */
#ifndef CONFIG_MACH_MAHIMAHI
		dev_wlc_ioctl(dev, WLC_SET_PM, &pm, sizeof(pm));
#endif

#ifdef COEX_DHCP
		/* Stop any bt timer because DHCP session is done */
		WL_TRACE(("%s disable BT DHCP Timer\n", __FUNCTION__));
		if (g_bt->timer_on) {
			g_bt->timer_on = 0;
			del_timer_sync(&g_bt->timer);
		}

		/* Restoring btc_flag paramter anyway */
		dev_wlc_bufvar_set(dev, "btc_flags",
		                   (char *)&buf_flag7_default[0], sizeof(buf_flag7_default));
#endif /* COEX_DHCP */

		/* Restore original values */
		if (saved_status) {
			regaddr = 66;
			dev_wlc_intvar_set_reg(dev, "btc_params",
				(char *)&regaddr, (char *)&saved_reg66);
			regaddr = 41;
			dev_wlc_intvar_set_reg(dev, "btc_params",
				(char *)&regaddr, (char *)&saved_reg41);
			regaddr = 68;
			dev_wlc_intvar_set_reg(dev, "btc_params",
				(char *)&regaddr, (char *)&saved_reg68);
		}
		saved_status = FALSE;

	}
	else {
		WL_ERROR(("Unkwown yet power setting, ignored\n"));
	}

	p += snprintf(p, MAX_WX_STRING, "OK");

	wrqu->data.length = p - extra + 1;

	return error;
}

static int
wl_iw_set_suspend(
struct net_device *dev,
struct iw_request_info *info,
union iwreq_data *wrqu,
char *extra
)
{
	int suspend_flag;
	int ret_now;
	int ret = 0;

	suspend_flag = *(extra + strlen(SETSUSPEND_CMD) + 1) - '0';

	if (suspend_flag != 0)
		suspend_flag = 1;

	ret_now = net_os_set_suspend_disable(dev, suspend_flag);

	/* Only if flag different from previolsy set force new settings */
	if (ret_now != suspend_flag) {
		if (!(ret = net_os_set_suspend(dev, ret_now)))
			WL_ERROR(("%s: Suspend Flag %d -> %d\n",
			          __FUNCTION__, ret_now, suspend_flag));
		else
			WL_ERROR(("%s: failed %d\n", __FUNCTION__, ret));
	}

	return ret;
}

static int
wl_format_ssid(char* ssid_buf, uint8* ssid, int ssid_len)
{
	int i, c;
	char *p = ssid_buf;

	if (ssid_len > 32) ssid_len = 32;

	for (i = 0; i < ssid_len; i++) {
		c = (int)ssid[i];
		if (c == '\\') {
			*p++ = '\\';
			*p++ = '\\';
		} else if (isprint((uchar)c)) {
			*p++ = (char)c;
		} else {
			p += sprintf(p, "\\x%02X", c);
		}
	}
	*p = '\0';

	return p - ssid_buf;
}

static int
wl_iw_get_link_speed(
	struct net_device *dev,
	struct iw_request_info *info,
	union iwreq_data *wrqu,
	char *extra
)
{
	int error = 0;
	char *p = extra;
	static int link_speed;

	/* Android may request link speed info even when chip is not up yet
	    return the previous link speed value
	*/
	net_os_wake_lock(dev);
	if (g_onoff == G_WLAN_SET_ON) {
		error = dev_wlc_ioctl(dev, WLC_GET_RATE, &link_speed, sizeof(link_speed));
		link_speed *= 500000;
	}

	p += snprintf(p, MAX_WX_STRING, "LinkSpeed %d", link_speed/1000000);

	wrqu->data.length = p - extra + 1;

	net_os_wake_unlock(dev);
	return error;
}

/*
 *   Get dtim skip current seting
 */
static int
wl_iw_get_dtim_skip(
	struct net_device *dev,
	struct iw_request_info *info,
	union iwreq_data *wrqu,
	char *extra
)
{
	int error = -1;
	char *p = extra;
	char iovbuf[32];

	net_os_wake_lock(dev);
	if (g_onoff == G_WLAN_SET_ON) {

			memset(iovbuf, 0, sizeof(iovbuf));
			strcpy(iovbuf, "bcn_li_dtim");

			if ((error = dev_wlc_ioctl(dev, WLC_GET_VAR,
				&iovbuf, sizeof(iovbuf))) >= 0) {

				p += snprintf(p, MAX_WX_STRING, "Dtim_skip %d", iovbuf[0]);
				WL_TRACE(("%s: get dtim_skip = %d\n", __FUNCTION__, iovbuf[0]));
				wrqu->data.length = p - extra + 1;
			}
			else
				WL_ERROR(("%s: get dtim_skip failed code %d\n",
					__FUNCTION__, error));
	}
	net_os_wake_unlock(dev);
	return error;
}

/*
 *   Set dtim_skip
 *   NOTE that when kernel suspended dtim_skip will be programmed as per
 *   dhd_set_suspend settings
 */
static int
wl_iw_set_dtim_skip(
	struct net_device *dev,
	struct iw_request_info *info,
	union iwreq_data *wrqu,
	char *extra
)
{
	int error = -1;
	char *p = extra;
	int bcn_li_dtim;
	char iovbuf[32];

	net_os_wake_lock(dev);
	if (g_onoff == G_WLAN_SET_ON) {

		bcn_li_dtim = htod32((uint)*(extra + strlen(DTIM_SKIP_SET_CMD) + 1) - '0');

		if ((bcn_li_dtim >= 0) || ((bcn_li_dtim <= 5))) {

			memset(iovbuf, 0, sizeof(iovbuf));
			bcm_mkiovar("bcn_li_dtim", (char *)&bcn_li_dtim,
				4, iovbuf, sizeof(iovbuf));

			if ((error = dev_wlc_ioctl(dev, WLC_SET_VAR,
				&iovbuf, sizeof(iovbuf))) >= 0) {
				p += snprintf(p, MAX_WX_STRING, "OK");

				/* save current dtim setting */
				net_os_set_dtim_skip(dev, bcn_li_dtim);

				WL_TRACE(("%s: set dtim_skip %d OK\n", __FUNCTION__,
					bcn_li_dtim));
				goto exit;
			}
			else  WL_ERROR(("%s: set dtim_skip %d failed code %d\n",
				__FUNCTION__, bcn_li_dtim, error));
		}
		else  WL_ERROR(("%s Incorrect dtim_skip setting %d, ignored\n",
			__FUNCTION__, bcn_li_dtim));
	}

	p += snprintf(p, MAX_WX_STRING, "FAIL");

exit:
	wrqu->data.length = p - extra + 1;
	net_os_wake_unlock(dev);
	return error;
}

/*
 *   Get band
 */
static int
wl_iw_get_band(
	struct net_device *dev,
	struct iw_request_info *info,
	union iwreq_data *wrqu,
	char *extra
)
{
	int error = -1;
	char *p = extra;
	static int band;

	net_os_wake_lock(dev);

	if (g_onoff == G_WLAN_SET_ON) {
		error = dev_wlc_ioctl(dev, WLC_GET_BAND, &band, sizeof(band));

		p += snprintf(p, MAX_WX_STRING, "Band %d", band);

		wrqu->data.length = p - extra + 1;
	}

	net_os_wake_unlock(dev);
	return error;
}

/*
 *   Set band
 */
static int
wl_iw_set_band(
	struct net_device *dev,
	struct iw_request_info *info,
	union iwreq_data *wrqu,
	char *extra
)
{
	int error = -1;
	char *p = extra;
	uint band;

	net_os_wake_lock(dev);

	if (g_onoff == G_WLAN_SET_ON) {

		band = htod32((uint)*(extra + strlen(BAND_SET_CMD) + 1) - '0');

		if ((band == WLC_BAND_AUTO) || (band == WLC_BAND_5G) || (band == WLC_BAND_2G)) {
			/* Band setting */
			if ((error = dev_wlc_ioctl(dev, WLC_SET_BAND,
				&band, sizeof(band))) >= 0) {
				p += snprintf(p, MAX_WX_STRING, "OK");
				WL_TRACE(("%s: set band %d OK\n", __FUNCTION__, band));
				goto exit;
			} else {
				WL_ERROR(("%s: set band %d failed code %d\n", __FUNCTION__,
				          band, error));
			}
		} else {
			WL_ERROR(("%s Incorrect band setting %d, ignored\n", __FUNCTION__, band));
		}
	}

	p += snprintf(p, MAX_WX_STRING, "FAIL");

exit:
	wrqu->data.length = p - extra + 1;
	net_os_wake_unlock(dev);
	return error;
}

#ifdef PNO_SUPPORT
/*
 *   Reset  PNO setting
 */
static int
wl_iw_set_pno_reset(
	struct net_device *dev,
	struct iw_request_info *info,
	union iwreq_data *wrqu,
	char *extra
)
{
	int error = -1;
	char *p = extra;

	net_os_wake_lock(dev);
	if ((g_onoff == G_WLAN_SET_ON) && (dev != NULL)) {

		if ((error = dhd_dev_pno_reset(dev)) >= 0) {
				p += snprintf(p, MAX_WX_STRING, "OK");
				WL_TRACE(("%s: set OK\n", __FUNCTION__));
				goto exit;
		}
		else  WL_ERROR(("%s: failed code %d\n", __FUNCTION__, error));
	}

	p += snprintf(p, MAX_WX_STRING, "FAIL");

exit:
	wrqu->data.length = p - extra + 1;
	net_os_wake_unlock(dev);
	return error;
}


/*
 *    PNO enable/disable
 */
static int
wl_iw_set_pno_enable(
	struct net_device *dev,
	struct iw_request_info *info,
	union iwreq_data *wrqu,
	char *extra
)
{
	int error = -1;
	char *p = extra;
	int pfn_enabled;

	net_os_wake_lock(dev);
	pfn_enabled = htod32((uint)*(extra + strlen(PNOENABLE_SET_CMD) + 1) - '0');

	if ((g_onoff == G_WLAN_SET_ON) && (dev != NULL)) {

		if ((error = dhd_dev_pno_enable(dev, pfn_enabled)) >= 0) {
				p += snprintf(p, MAX_WX_STRING, "OK");
				WL_TRACE(("%s: set OK\n", __FUNCTION__));
				goto exit;
		}
		else  WL_ERROR(("%s: failed code %d\n", __FUNCTION__, error));
	}

	p += snprintf(p, MAX_WX_STRING, "FAIL");

exit:
	wrqu->data.length = p - extra + 1;
	net_os_wake_unlock(dev);
	return error;
}


/*
 *   Setting PNO from TLV-based binary input
 */
static int
wl_iw_set_pno_set(
	struct net_device *dev,
	struct iw_request_info *info,
	union iwreq_data *wrqu,
	char *extra
)
{
	int res = -1;
	wlc_ssid_t ssids_local[MAX_PFN_LIST_COUNT];
	int nssid = 0;
	cmd_tlv_t *cmd_tlv_temp;
	char type;
	char *str_ptr;
	int tlv_size_left;
	int pno_time;
/* #define  PNO_SET_DEBUG  1 */
#ifdef PNO_SET_DEBUG
	int i;
	char pno_in_example[] = {
		'P', 'N', 'O', 'S', 'E', 'T', 'U', 'P', ' ',
		'S', 0x01, 0x00, 0x00,
		'S',    /* SSID type */
		0x04, /* SSID size */
		'B', 'R', 'C', 'M',
		'S',    /* SSID type */
		0x04, /* SSID size */
		'G', 'O', 'O', 'G',
		'T',
		0x0A, /* time 10 sec */
	};
#endif /* PNO_SET_DEBUG */

	net_os_wake_lock(dev);
	WL_ERROR(("\n### %s: info->cmd:%x, info->flags:%x, u.data=0x%p, u.len=%d\n",
		__FUNCTION__, info->cmd, info->flags,
		wrqu->data.pointer, wrqu->data.length));

	if (g_onoff == G_WLAN_SET_OFF) {
		WL_TRACE(("%s: driver is not up yet after START\n", __FUNCTION__));
		goto exit_proc;
	}

	if (wrqu->data.length < (strlen(PNOSETUP_SET_CMD) + sizeof(cmd_tlv_t))) {
		WL_ERROR(("%s aggument=%d  less %d\n", __FUNCTION__,
			wrqu->data.length, strlen(PNOSETUP_SET_CMD) + sizeof(cmd_tlv_t)));
		goto exit_proc;
	}

#ifdef PNO_SET_DEBUG
	if (!(extra = kmalloc(sizeof(pno_in_example) +100, GFP_KERNEL))) {
		res = -ENOMEM;
		goto exit_proc;
	}
	memcpy(extra, pno_in_example, sizeof(pno_in_example));
	wrqu->data.length = sizeof(pno_in_example);
	for (i = 0; i < wrqu->data.length; i++)
		printf("%02X ", extra[i]);
	printf("\n");
#endif /* PNO_SET_DEBUG */

	str_ptr = extra;
#ifdef PNO_SET_DEBUG
	str_ptr +=  strlen("PNOSETUP ");
	tlv_size_left = wrqu->data.length - strlen("PNOSETUP ");
#else
	str_ptr +=  strlen(PNOSETUP_SET_CMD);
	tlv_size_left = wrqu->data.length - strlen(PNOSETUP_SET_CMD);
#endif

	cmd_tlv_temp = (cmd_tlv_t *)str_ptr;
	memset(ssids_local, 0, sizeof(ssids_local));

	/* PNO TLV command must start with predefined
	* prefixes and first paramter should be for SSID
	*/
	if ((cmd_tlv_temp->prefix != PNO_TLV_PREFIX) ||
	    (cmd_tlv_temp->version != PNO_TLV_VERSION) ||
	    (cmd_tlv_temp->subver != PNO_TLV_SUBVERSION)) {
		WL_ERROR(("%s: wrong TLV command\n", __FUNCTION__));
		goto exit_proc;
	}

	str_ptr += sizeof(cmd_tlv_t);
	tlv_size_left  -= sizeof(cmd_tlv_t);

	/* Get SSIDs list */
	if ((nssid = wl_iw_parse_ssid_list_tlv(&str_ptr, ssids_local,
	                                       MAX_PFN_LIST_COUNT, &tlv_size_left)) <= 0) {
		WL_ERROR(("SSID is not presented or corrupted ret=%d\n", nssid));
		goto exit_proc;
	}

	/* Get other params */
	while (tlv_size_left > 0) {
		type = str_ptr[0];
		switch (type) {
		case PNO_TLV_TYPE_TIME:
			/* Search for PNO time info */
			if ((res = wl_iw_parse_data_tlv(&str_ptr,
			                                &pno_time, sizeof(pno_time),
			                                type, sizeof(char),
			                                &tlv_size_left)) == -1) {
				WL_ERROR(("%s return %d\n",
				          __FUNCTION__, res));
				goto exit_proc;
			}
			break;

		default:
			WL_ERROR(("%s get unkwown type %X\n",
			          __FUNCTION__, type));
			goto exit_proc;
			break;
		}
	}

	/* PNO setting execution */
	res = dhd_dev_pno_set(dev, ssids_local, nssid, pno_time);

exit_proc:
	net_os_wake_unlock(dev);
	return res;
}
#endif /* PNO_SUPPORT */

static int
wl_iw_get_rssi(
	struct net_device *dev,
	struct iw_request_info *info,
	union iwreq_data *wrqu,
	char *extra
)
{
	static int rssi = 0;
	static wlc_ssid_t ssid = {0};
	int error = 0;
	char *p = extra;
	static char ssidbuf[SSID_FMT_BUF_LEN];
	scb_val_t scb_val;

	net_os_wake_lock(dev);

	bzero(&scb_val, sizeof(scb_val_t));

	if (g_onoff == G_WLAN_SET_ON) {
		error = dev_wlc_ioctl(dev, WLC_GET_RSSI, &scb_val, sizeof(scb_val_t));
		if (error) {
			WL_ERROR(("%s: Fails %d\n", __FUNCTION__, error));
			net_os_wake_unlock(dev);
			return error;
		}
		rssi = dtoh32(scb_val.val);

		error = dev_wlc_ioctl(dev, WLC_GET_SSID, &ssid, sizeof(ssid));
		if (!error) {
			ssid.SSID_len = dtoh32(ssid.SSID_len);
			wl_format_ssid(ssidbuf, ssid.SSID, dtoh32(ssid.SSID_len));
		}
	}

	p += snprintf(p, MAX_WX_STRING, "%s rssi %d ", ssidbuf, rssi);
	wrqu->data.length = p - extra + 1;

	net_os_wake_unlock(dev);
	return error;
}

int
wl_iw_send_priv_event(
	struct net_device *dev,
	char *flag
)
{
	union iwreq_data wrqu;
	char extra[IW_CUSTOM_MAX + 1];
	int cmd;

	cmd = IWEVCUSTOM;
	memset(&wrqu, 0, sizeof(wrqu));
	if (strlen(flag) > sizeof(extra))
		return -1;

	strcpy(extra, flag);
	wrqu.data.length = strlen(extra);
	wireless_send_event(dev, cmd, &wrqu, extra);
	net_os_wake_lock_timeout_enable(dev);
	WL_TRACE(("Send IWEVCUSTOM Event as %s\n", extra));

	return 0;
}


int
wl_control_wl_start(struct net_device *dev)
{
	int ret = 0;

	WL_TRACE(("Enter %s \n", __FUNCTION__));

	if (!dev) {
		WL_ERROR(("%s: dev is null\n", __FUNCTION__));
		return -1;
	}
	/* wl_start_lock is initialized in wl_iw_attach[wl_iw.c] */
	DHD_OS_MUTEX_LOCK(&wl_start_lock);

	if (g_onoff == G_WLAN_SET_OFF) {
		dhd_customer_gpio_wlan_ctrl(WLAN_RESET_ON);

#if defined(BCMLXSDMMC)
		sdioh_start(NULL, 0);
#endif

		dhd_dev_reset(dev, 0);

#if defined(BCMLXSDMMC)
		sdioh_start(NULL, 1);
#endif

		dhd_dev_init_ioctl(dev);

		g_onoff = G_WLAN_SET_ON;
	}
	WL_ERROR(("Exited %s \n", __FUNCTION__));

	DHD_OS_MUTEX_UNLOCK(&wl_start_lock);
	return ret;
}


static int
wl_iw_control_wl_off(
	struct net_device *dev,
	struct iw_request_info *info
)
{
	int ret = 0;

	WL_ERROR(("Enter %s\n", __FUNCTION__));

	if (!dev) {
		WL_ERROR(("%s: dev is null\n", __FUNCTION__));
		return -1;
	}

	DHD_OS_MUTEX_LOCK(&wl_start_lock);

#ifdef SOFTAP
	ap_cfg_running = FALSE;
#endif /* SOFTAP */

	if (g_onoff == G_WLAN_SET_ON) {
		g_onoff = G_WLAN_SET_OFF;

#if defined(WL_IW_USE_ISCAN)
		g_iscan->iscan_state = ISCAN_STATE_IDLE;
#endif /* (WL_IW_USE_ISCAN) */

		dhd_dev_reset(dev, 1);

#if defined(WL_IW_USE_ISCAN)
#if !defined(CSCAN)
		/* Dongle is off, clean up all history */
		wl_iw_free_ss_cache();
		wl_iw_run_ss_cache_timer(0);
		/* always set cache linkd down flag with stop */
		g_ss_cache_ctrl.m_link_down = 1;
#endif /* !defined(CSCAN) */
		memset(g_scan, 0, G_SCAN_RESULTS);
		g_scan_specified_ssid = 0;
		/* reset flag so broadcast scan will be forced first after start */
		g_first_broadcast_scan = BROADCAST_SCAN_FIRST_IDLE;
		g_first_counter_scans = 0;
#endif /* defined(WL_IW_USE_ISCAN) */

#if defined(BCMLXSDMMC)
		sdioh_stop(NULL);
#endif

		/* clean up dtim_skip setting */
		net_os_set_dtim_skip(dev, 0);

		dhd_customer_gpio_wlan_ctrl(WLAN_RESET_OFF);

		wl_iw_send_priv_event(dev, "STOP");
	}

	DHD_OS_MUTEX_UNLOCK(&wl_start_lock);

	WL_TRACE(("Exited %s\n", __FUNCTION__));

	return ret;
}

static int
wl_iw_control_wl_on(
	struct net_device *dev,
	struct iw_request_info *info
)
{
	int ret = 0;

	WL_TRACE(("Enter %s \n", __FUNCTION__));

	ret = wl_control_wl_start(dev);

	wl_iw_send_priv_event(dev, "START");

#ifdef SOFTAP
	if (!ap_fw_loaded) {
		wl_iw_iscan_set_scan_broadcast_prep(dev, 0);
	}
#else
	wl_iw_iscan_set_scan_broadcast_prep(dev, 0);
#endif

	WL_TRACE(("Exited %s \n", __FUNCTION__));

	return ret;
}

#ifdef SOFTAP
static struct ap_profile my_ap;
static int set_ap_cfg(struct net_device *dev, struct ap_profile *ap); /* fwd decl */
static int get_assoc_sta_list(struct net_device *dev, char *buf, int len);
static int set_ap_mac_list(struct net_device *dev, char *buf);

#define PTYPE_STRING 0
#define PTYPE_INTDEC 1   /* ascii string representing  decimal integer number */
#define PTYPE_INTHEX 2
#define PTYPE_STR_HEX 3  /*  ascii string representing HEX buffer */

static int get_parameter_from_string(
	char **str_ptr, const char *token, int param_type, void  *dst, int param_max_len);

#endif /* SOFTAP */

static int
hex2num(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}


/**
 * Convert ASCII string to  byte array MAC address
 * input: an ASCII HEX string (e.g., "001122334455AABBCCDDEEFF")
 * output: fills up a buffer pointed by *buf var
 * Returns: 0 on success, -1 on failure (e.g., ASCII string uses non HEX chars)
 */
static int
hstr_2_buf(const char *txt, u8 *buf, int len)
{
	int i;

	for (i = 0; i < len; i++) {
		int a, b;

		a = hex2num(*txt++);
		if (a < 0)
			return -1;
		b = hex2num(*txt++);
		if (b < 0)
			return -1;
		*buf++ = (a << 4) | b;
	}

	return 0;
}

/*
*    ******* initialize ap_profile structure from ASCII_CMD string *******
*     eg: "ASCII_CMD=AP_CFG,SSID=SSID_BLAH,SEC=wpa2-psk,KEY=123456..."
*    *********************************************************************
*/
#ifdef SOFTAP
static int
init_ap_profile_from_string(char *param_str, struct ap_profile *ap_cfg)
{
	char *str_ptr = param_str;
	char sub_cmd[16];
	int ret = 0;

	memset(sub_cmd, 0, sizeof(sub_cmd));
	memset(ap_cfg, 0, sizeof(struct ap_profile));

	/*  make sure the  ASCII_CMD='AP_CFG' */
	if (get_parameter_from_string(&str_ptr, "ASCII_CMD=",
		PTYPE_STRING, sub_cmd, SSID_LEN) != 0) {
	 return -1;
	}
	if (strncmp(sub_cmd, "AP_CFG", 6)) {
	   WL_ERROR(("ERROR: sub_cmd:%s != 'AP_CFG'!\n", sub_cmd));
		return -1;
	}

	/*  parse the string and write extracted values into the ap_profile structure */
	/*  NOTE this function may alter the origibal string */
	ret = get_parameter_from_string(&str_ptr, "SSID=", PTYPE_STRING, ap_cfg->ssid, SSID_LEN);

	ret |= get_parameter_from_string(&str_ptr, "SEC=", PTYPE_STRING,  ap_cfg->sec, SEC_LEN);

	ret |= get_parameter_from_string(&str_ptr, "KEY=", PTYPE_STRING,  ap_cfg->key, KEY_LEN);

	ret |= get_parameter_from_string(&str_ptr, "CHANNEL=", PTYPE_INTDEC, &ap_cfg->channel, 5);

	ret |= get_parameter_from_string(&str_ptr, "PREAMBLE=", PTYPE_INTDEC, &ap_cfg->preamble, 5);

	ret |= get_parameter_from_string(&str_ptr, "MAX_SCB=", PTYPE_INTDEC,  &ap_cfg->max_scb, 5);

	return ret;
}
#endif /* SOFTAP */


/*
*  called by iwpriv AP_SET_CFG
*  pasing all Ap setting params and store it into my_ap structure
*  and call set_ap_cfg which is going to place Dongle into AP mode
*  Returns zero if OK
*/
#ifdef SOFTAP
static int
iwpriv_set_ap_config(struct net_device *dev,
            struct iw_request_info *info,
            union iwreq_data *wrqu,
            char *ext)
{
	int res = 0;
	char  *extra = NULL;
	struct ap_profile *ap_cfg = &my_ap;

	WL_TRACE(("> Got IWPRIV SET_AP IOCTL: info->cmd:%x, info->flags:%x, u.data:%p, u.len:%d\n",
		info->cmd, info->flags,
		wrqu->data.pointer, wrqu->data.length));

	if (wrqu->data.length != 0) {

		char *str_ptr;

		if (!(extra = kmalloc(wrqu->data.length+1, GFP_KERNEL)))
			return -ENOMEM;

		if (copy_from_user(extra, wrqu->data.pointer, wrqu->data.length)) {
			kfree(extra);
			return -EFAULT;
		}

		extra[wrqu->data.length] = 0;
		WL_SOFTAP((" Got str param in iw_point:\n %s\n", extra));

		memset(ap_cfg, 0, sizeof(struct ap_profile));

		/*  parse param string and write extracted values into the ap_profile structure */

		str_ptr = extra;

		if ((res = init_ap_profile_from_string(extra, ap_cfg)) < 0) {
			WL_ERROR(("%s failed to parse %d\n", __FUNCTION__, res));
			kfree(extra);
			return -1;
		}

	} else {
	 /* len is zero */
	  WL_ERROR(("IWPRIV argument len = 0 \n"));
	  return -1;
	}

	if ((res = set_ap_cfg(dev, ap_cfg)) < 0)
		WL_ERROR(("%s failed to set_ap_cfg %d\n", __FUNCTION__, res));

	kfree(extra);

	return res;
}
#endif /* SOFTAP */


/*
*   ************ get list of associated stations ********
*/
#ifdef SOFTAP
static int
iwpriv_get_assoc_list(struct net_device *dev,
        struct iw_request_info *info,
        union iwreq_data *p_iwrq,
        char *extra)
{
	int i, ret = 0;
	char mac_buf[256];
	struct maclist *sta_maclist = (struct maclist *)mac_buf;

	char mac_lst[256];
	char *p_mac_str;

	WL_TRACE(("%s: IWPRIV IOCTL: cmd:%hx, flags:%hx, extra:%p, iwp.len:%d, "
	          "iwp.len:%p, iwp.flags:%x\n", __FUNCTION__, info->cmd, info->flags,
	          extra, p_iwrq->data.length, p_iwrq->data.pointer, p_iwrq->data.flags));

	WL_SOFTAP(("extra:%s\n", extra));
	dhd_print_buf((u8 *)p_iwrq, 16, 0);

	memset(sta_maclist, 0, sizeof(mac_buf));

	sta_maclist->count = 8;

	WL_TRACE((" net device:%s, buf_sz:%d\n", dev->name, sizeof(mac_buf)));
	get_assoc_sta_list(dev, mac_buf, 256);
	WL_TRACE((" got %d stations\n", sta_maclist->count));

	/* convert maclist to ascii string */
	memset(mac_lst, 0, sizeof(mac_lst));
	p_mac_str = mac_lst;

	for (i = 0; i < 8; i++) {
		struct ether_addr * id = &sta_maclist->ea[i];

		WL_SOFTAP(("dhd_drv>> sta_mac[%d] :", i));
		dhd_print_buf((unsigned char *)&sta_maclist->ea[i], 6, 0);

		/* print all 8 macs into one string to be returned to the userspace  */
		p_mac_str += snprintf(p_mac_str, MAX_WX_STRING,
			"Mac[%d]=%02X:%02X:%02X:%02X:%02X:%02X\n", i,
			id->octet[0], id->octet[1], id->octet[2],
			id->octet[3], id->octet[4], id->octet[5]);

	}

	p_iwrq->data.length = strlen(mac_lst);

	WL_TRACE(("u.pointer:%p\n", p_iwrq->data.pointer));
	WL_TRACE(("resulting str:\n%s \n len:%d\n\n", mac_lst, p_iwrq->data.length));

	if (p_iwrq->data.length) {
		if (copy_to_user(p_iwrq->data.pointer, mac_lst, p_iwrq->data.length)) {
			WL_ERROR(("%s: Can't copy to user\n", __FUNCTION__));
			return -EFAULT;
		}
	}

	WL_ERROR(("Exited %s \n", __FUNCTION__));
	return ret;
}
#endif /* SOFTAP */

/*
*   ***************** set mac filters  *****************
*/
#ifdef SOFTAP
static int
iwpriv_set_mac_filters(struct net_device *dev,
        struct iw_request_info *info,
        union iwreq_data *wrqu,
        char *ext)
{

	int i, ret = -1;
	char  * extra = NULL;
	u8  macfilt[8][6];
	int mac_cnt = 0; /* number of MAC filters */
	char sub_cmd[16];
	char *str_ptr;

	WL_TRACE((">>> Got IWPRIV SET_MAC_FILTER IOCTL: info->cmd:%x, "
	          "info->flags:%x, u.data:%p, u.len:%d\n",
	          info->cmd, info->flags,
	          wrqu->data.pointer, wrqu->data.length));

	if (wrqu->data.length == 0) {
		WL_ERROR(("IWPRIV argument len is 0\n"));
		return -EINVAL;
	}

	if (!(extra = kmalloc(wrqu->data.length+1, GFP_KERNEL)))
		return -ENOMEM;

	if (copy_from_user(extra, wrqu->data.pointer, wrqu->data.length)) {
		ret = -EFAULT;
		goto exit_proc;
	}

	extra[wrqu->data.length] = 0;
	WL_SOFTAP((" Got parameter string in iw_point:\n %s \n", extra));

	memset(macfilt, 0, sizeof(macfilt));
	memset(sub_cmd, 0, sizeof(sub_cmd));

	/*  parse param string and write extracted values into the ap_profile structure */
	str_ptr = extra;

	/*  make sure that 1st token 'ASCII_CMD' */
	if (get_parameter_from_string(&str_ptr, "ASCII_CMD=", PTYPE_STRING, sub_cmd, 15) != 0) {
		goto exit_proc;
	}

#define MAC_FILT_MAX 8
	/* and  sub_cmd  WHITE list or black list */
	if (strncmp(sub_cmd, "MAC_FLT_W", strlen("MAC_FLT_W"))) {
		WL_ERROR(("ERROR: sub_cmd:%s != 'MAC_FLT_W'!\n", sub_cmd));
		goto exit_proc;
	}

	if (get_parameter_from_string(&str_ptr, "MAC_CNT=",
	                              PTYPE_INTDEC, &mac_cnt, 4) != 0) {
		WL_ERROR(("ERROR: MAC_CNT param is missing \n"));
		goto exit_proc;
	}

	if (mac_cnt > MAC_FILT_MAX) {
		WL_ERROR(("ERROR: number of MAC filters > MAX\n"));
		goto exit_proc;
	}

	for (i = 0; i < mac_cnt; i++)	/* get up to 8 MACs */
		if (get_parameter_from_string(&str_ptr, "MAC=",
		                              PTYPE_STR_HEX, macfilt[i], 12) != 0) {
			WL_ERROR(("ERROR: MAC_filter[%d] is missing !\n", i));
			goto exit_proc;
		}

	for (i = 0; i < mac_cnt; i++) {
		WL_SOFTAP(("mac_filt[%d]:", i));
		dhd_print_buf(macfilt[i], 6, 0);
	}

	/* now to return the same shit back to iwpriv */
	wrqu->data.pointer = NULL;
	wrqu->data.length = 0;

	ret = 0;

exit_proc:
	kfree(extra);
	return ret;
}
#endif /* SOFTAP */

#endif /* WIRELESS_EXT > 12 */

#if WIRELESS_EXT < 13
struct iw_request_info
{
	__u16		cmd;		/* Wireless Extension command */
	__u16		flags;		/* More to come ;-) */
};

typedef int (*iw_handler)(struct net_device *dev,
                struct iw_request_info *info,
                void *wrqu,
                char *extra);
#endif /* WIRELESS_EXT < 13 */

static int
wl_iw_config_commit(
	struct net_device *dev,
	struct iw_request_info *info,
	void *zwrq,
	char *extra
)
{
	wlc_ssid_t ssid;
	int error;
	struct sockaddr bssid;

	WL_TRACE(("%s: SIOCSIWCOMMIT\n", dev->name));

	if ((error = dev_wlc_ioctl(dev, WLC_GET_SSID, &ssid, sizeof(ssid))))
		return error;

	ssid.SSID_len = dtoh32(ssid.SSID_len);

	if (!ssid.SSID_len)
		return 0;

	bzero(&bssid, sizeof(struct sockaddr));
	if ((error = dev_wlc_ioctl(dev, WLC_REASSOC, &bssid, ETHER_ADDR_LEN))) {
		WL_ERROR(("%s: WLC_REASSOC to %s failed \n", __FUNCTION__, ssid.SSID));
		return error;
	}

	return 0;
}

static int
wl_iw_get_name(
	struct net_device *dev,
	struct iw_request_info *info,
	char *cwrq,
	char *extra
)
{
	WL_TRACE(("%s: SIOCGIWNAME\n", dev->name));

	strcpy(cwrq, "IEEE 802.11-DS");

	return 0;
}

static int
wl_iw_set_freq(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_freq *fwrq,
	char *extra
)
{
	int error, chan;
	uint sf = 0;

	WL_TRACE(("%s %s: SIOCSIWFREQ\n", __FUNCTION__, dev->name));
/*   scan is not allowed in AP mode  */
#if defined(SOFTAP)
	if (ap_cfg_running) {
		WL_TRACE(("%s:>> not executed, 'SOFT_AP is active' \n", __FUNCTION__));
		return 0;
	}
#endif

	/* Setting by channel number */
	if (fwrq->e == 0 && fwrq->m < MAXCHANNEL) {
		chan = fwrq->m;
	}
	/* Setting by frequency */
	else {
		/* Convert to MHz as best we can */
		if (fwrq->e >= 6) {
			fwrq->e -= 6;
			while (fwrq->e--)
				fwrq->m *= 10;
		} else if (fwrq->e < 6) {
			while (fwrq->e++ < 6)
				fwrq->m /= 10;
		}
		/* handle 4.9GHz frequencies as Japan 4 GHz based channelization */
		if (fwrq->m > 4000 && fwrq->m < 5000)
			sf = WF_CHAN_FACTOR_4_G; /* start factor for 4 GHz */

		chan = wf_mhz2channel(fwrq->m, sf);
	}

	chan = htod32(chan);

	if ((error = dev_wlc_ioctl(dev, WLC_SET_CHANNEL, &chan, sizeof(chan))))
		return error;

	g_wl_iw_params.target_channel = chan;

	/* -EINPROGRESS: Call commit handler */
	return -EINPROGRESS;
}

static int
wl_iw_get_freq(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_freq *fwrq,
	char *extra
)
{
	channel_info_t ci;
	int error;

	WL_TRACE(("%s: SIOCGIWFREQ\n", dev->name));

	if ((error = dev_wlc_ioctl(dev, WLC_GET_CHANNEL, &ci, sizeof(ci))))
		return error;

	/* Return radio channel in channel form */
	fwrq->m = dtoh32(ci.hw_channel);
	fwrq->e = dtoh32(0);
	return 0;
}

static int
wl_iw_set_mode(
	struct net_device *dev,
	struct iw_request_info *info,
	__u32 *uwrq,
	char *extra
)
{
	int infra = 0, ap = 0, error = 0;

	WL_TRACE(("%s: SIOCSIWMODE\n", dev->name));

	switch (*uwrq) {
	case IW_MODE_MASTER:
		infra = ap = 1;
		break;
	case IW_MODE_ADHOC:
	case IW_MODE_AUTO:
		break;
	case IW_MODE_INFRA:
		infra = 1;
		break;
	default:
		return -EINVAL;
	}
	infra = htod32(infra);
	ap = htod32(ap);

	if ((error = dev_wlc_ioctl(dev, WLC_SET_INFRA, &infra, sizeof(infra))) ||
	    (error = dev_wlc_ioctl(dev, WLC_SET_AP, &ap, sizeof(ap))))
		return error;

	/* -EINPROGRESS: Call commit handler */
	return -EINPROGRESS;
}

static int
wl_iw_get_mode(
	struct net_device *dev,
	struct iw_request_info *info,
	__u32 *uwrq,
	char *extra
)
{
	int error, infra = 0, ap = 0;

	WL_TRACE(("%s: SIOCGIWMODE\n", dev->name));

	if ((error = dev_wlc_ioctl(dev, WLC_GET_INFRA, &infra, sizeof(infra))) ||
	    (error = dev_wlc_ioctl(dev, WLC_GET_AP, &ap, sizeof(ap))))
		return error;

	infra = dtoh32(infra);
	ap = dtoh32(ap);
	*uwrq = infra ? ap ? IW_MODE_MASTER : IW_MODE_INFRA : IW_MODE_ADHOC;

	return 0;
}

static int
wl_iw_get_range(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_point *dwrq,
	char *extra
)
{
	struct iw_range *range = (struct iw_range *) extra;
	wl_uint32_list_t *list;
	wl_rateset_t rateset;
	int8 *channels;
	int error, i, k;
	uint sf, ch;

	int phytype;
	int bw_cap = 0, sgi_tx = 0, nmode = 0;
	channel_info_t ci;
	uint8 nrate_list2copy = 0;
	uint16 nrate_list[4][8] = { {13, 26, 39, 52, 78, 104, 117, 130},
		{14, 29, 43, 58, 87, 116, 130, 144},
		{27, 54, 81, 108, 162, 216, 243, 270},
		{30, 60, 90, 120, 180, 240, 270, 300}};

	WL_TRACE(("%s: SIOCGIWRANGE\n", dev->name));

	if (!extra)
		return -EINVAL;

	channels = kmalloc((MAXCHANNEL+1)*4, GFP_KERNEL);
	if (!channels) {
		WL_ERROR(("Could not alloc channels\n"));
		return -ENOMEM;
	}
	list = (wl_uint32_list_t *)channels;

	dwrq->length = sizeof(struct iw_range);
	memset(range, 0, sizeof(range));

	/* We don't use nwids */
	range->min_nwid = range->max_nwid = 0;

	/* Set available channels/frequencies */
	list->count = htod32(MAXCHANNEL);
	if ((error = dev_wlc_ioctl(dev, WLC_GET_VALID_CHANNELS, channels, (MAXCHANNEL+1)*4))) {
		kfree(channels);
		return error;
	}
	for (i = 0; i < dtoh32(list->count) && i < IW_MAX_FREQUENCIES; i++) {
		range->freq[i].i = dtoh32(list->element[i]);

		ch = dtoh32(list->element[i]);
		if (ch <= CH_MAX_2G_CHANNEL)
			sf = WF_CHAN_FACTOR_2_4_G;
		else
			sf = WF_CHAN_FACTOR_5_G;

		range->freq[i].m = wf_channel2mhz(ch, sf);
		range->freq[i].e = 6;
	}
	range->num_frequency = range->num_channels = i;

	/* Link quality (use NDIS cutoffs) */
	range->max_qual.qual = 5;
	/* Signal level (use RSSI) */
	range->max_qual.level = 0x100 - 200;	/* -200 dBm */
	/* Noise level (use noise) */
	range->max_qual.noise = 0x100 - 200;	/* -200 dBm */
	/* Signal level threshold range (?) */
	range->sensitivity = 65535;

#if WIRELESS_EXT > 11
	/* Link quality (use NDIS cutoffs) */
	range->avg_qual.qual = 3;
	/* Signal level (use RSSI) */
	range->avg_qual.level = 0x100 + WL_IW_RSSI_GOOD;
	/* Noise level (use noise) */
	range->avg_qual.noise = 0x100 - 75;	/* -75 dBm */
#endif /* WIRELESS_EXT > 11 */

	/* Set available bitrates */
	if ((error = dev_wlc_ioctl(dev, WLC_GET_CURR_RATESET, &rateset, sizeof(rateset)))) {
		kfree(channels);
		return error;
	}
	rateset.count = dtoh32(rateset.count);
	range->num_bitrates = rateset.count;
	for (i = 0; i < rateset.count && i < IW_MAX_BITRATES; i++)
		range->bitrate[i] = (rateset.rates[i]& 0x7f) * 500000; /* convert to bps */
	dev_wlc_intvar_get(dev, "nmode", &nmode);
	dev_wlc_ioctl(dev, WLC_GET_PHYTYPE, &phytype, sizeof(phytype));

	if (nmode == 1 && phytype == WLC_PHY_TYPE_SSN) {
		dev_wlc_intvar_get(dev, "mimo_bw_cap", &bw_cap);
		dev_wlc_intvar_get(dev, "sgi_tx", &sgi_tx);
		dev_wlc_ioctl(dev, WLC_GET_CHANNEL, &ci, sizeof(channel_info_t));
		ci.hw_channel = dtoh32(ci.hw_channel);

		if (bw_cap == 0 ||
			(bw_cap == 2 && ci.hw_channel <= 14)) {
			if (sgi_tx == 0)
				nrate_list2copy = 0;
			else
				nrate_list2copy = 1;
		}
		if (bw_cap == 1 ||
			(bw_cap == 2 && ci.hw_channel >= 36)) {
			if (sgi_tx == 0)
				nrate_list2copy = 2;
			else
				nrate_list2copy = 3;
		}
		range->num_bitrates += 8;
		for (k = 0; i < range->num_bitrates; k++, i++) {
			/* convert to bps */
			range->bitrate[i] = (nrate_list[nrate_list2copy][k]) * 500000;
		}
	}

	/* Set an indication of the max TCP throughput
	 * in bit/s that we can expect using this interface.
	 * May be use for QoS stuff... Jean II
	 */
	if ((error = dev_wlc_ioctl(dev, WLC_GET_PHYTYPE, &i, sizeof(i)))) {
		kfree(channels);
		return error;
	}
	i = dtoh32(i);
	if (i == WLC_PHY_TYPE_A)
		range->throughput = 24000000;	/* 24 Mbits/s */
	else
		range->throughput = 1500000;	/* 1.5 Mbits/s */

	/* RTS and fragmentation thresholds */
	range->min_rts = 0;
	range->max_rts = 2347;
	range->min_frag = 256;
	range->max_frag = 2346;

	range->max_encoding_tokens = DOT11_MAX_DEFAULT_KEYS;
	range->num_encoding_sizes = 4;
	range->encoding_size[0] = WEP1_KEY_SIZE;
	range->encoding_size[1] = WEP128_KEY_SIZE;
#if WIRELESS_EXT > 17
	range->encoding_size[2] = TKIP_KEY_SIZE;
#else
	range->encoding_size[2] = 0;
#endif
	range->encoding_size[3] = AES_KEY_SIZE;

	/* Do not support power micro-management */
	range->min_pmp = 0;
	range->max_pmp = 0;
	range->min_pmt = 0;
	range->max_pmt = 0;
	range->pmp_flags = 0;
	range->pm_capa = 0;

	/* Transmit Power - values are in mW */
	range->num_txpower = 2;
	range->txpower[0] = 1;
	range->txpower[1] = 255;
	range->txpower_capa = IW_TXPOW_MWATT;

#if WIRELESS_EXT > 10
	range->we_version_compiled = WIRELESS_EXT;
	range->we_version_source = 19;

	/* Only support retry limits */
	range->retry_capa = IW_RETRY_LIMIT;
	range->retry_flags = IW_RETRY_LIMIT;
	range->r_time_flags = 0;
	/* SRL and LRL limits */
	range->min_retry = 1;
	range->max_retry = 255;
	/* Retry lifetime limits unsupported */
	range->min_r_time = 0;
	range->max_r_time = 0;
#endif /* WIRELESS_EXT > 10 */

#if WIRELESS_EXT > 17
	range->enc_capa = IW_ENC_CAPA_WPA;
	range->enc_capa |= IW_ENC_CAPA_CIPHER_TKIP;
	range->enc_capa |= IW_ENC_CAPA_CIPHER_CCMP;
	range->enc_capa |= IW_ENC_CAPA_WPA2;

	/* Event capability (kernel) */
	IW_EVENT_CAPA_SET_KERNEL(range->event_capa);
	/* Event capability (driver) */
	IW_EVENT_CAPA_SET(range->event_capa, SIOCGIWAP);
	IW_EVENT_CAPA_SET(range->event_capa, SIOCGIWSCAN);
	IW_EVENT_CAPA_SET(range->event_capa, IWEVTXDROP);
	IW_EVENT_CAPA_SET(range->event_capa, IWEVMICHAELMICFAILURE);
	IW_EVENT_CAPA_SET(range->event_capa, IWEVPMKIDCAND);
#endif /* WIRELESS_EXT > 17 */

	kfree(channels);

	return 0;
}

static int
rssi_to_qual(int rssi)
{
	if (rssi <= WL_IW_RSSI_NO_SIGNAL)
		return 0;
	else if (rssi <= WL_IW_RSSI_VERY_LOW)
		return 1;
	else if (rssi <= WL_IW_RSSI_LOW)
		return 2;
	else if (rssi <= WL_IW_RSSI_GOOD)
		return 3;
	else if (rssi <= WL_IW_RSSI_VERY_GOOD)
		return 4;
	else
		return 5;
}

static int
wl_iw_set_spy(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_point *dwrq,
	char *extra
)
{
	wl_iw_t *iw = NETDEV_PRIV(dev);
	struct sockaddr *addr = (struct sockaddr *) extra;
	int i;

	WL_TRACE(("%s: SIOCSIWSPY\n", dev->name));

	if (!extra)
		return -EINVAL;

	iw->spy_num = MIN(ARRAYSIZE(iw->spy_addr), dwrq->length);
	for (i = 0; i < iw->spy_num; i++)
		memcpy(&iw->spy_addr[i], addr[i].sa_data, ETHER_ADDR_LEN);
	memset(iw->spy_qual, 0, sizeof(iw->spy_qual));

	return 0;
}

static int
wl_iw_get_spy(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_point *dwrq,
	char *extra
)
{
	wl_iw_t *iw = NETDEV_PRIV(dev);
	struct sockaddr *addr = (struct sockaddr *) extra;
	struct iw_quality *qual = (struct iw_quality *) &addr[iw->spy_num];
	int i;

	WL_TRACE(("%s: SIOCGIWSPY\n", dev->name));

	if (!extra)
		return -EINVAL;

	dwrq->length = iw->spy_num;
	for (i = 0; i < iw->spy_num; i++) {
		memcpy(addr[i].sa_data, &iw->spy_addr[i], ETHER_ADDR_LEN);
		addr[i].sa_family = AF_UNIX;
		memcpy(&qual[i], &iw->spy_qual[i], sizeof(struct iw_quality));
		iw->spy_qual[i].updated = 0;
	}

	return 0;
}

/*
 *  Convert channel number to chanspec structure
*/
static int
wl_iw_ch_to_chanspec(int ch, wl_join_params_t *join_params, int *join_params_size)
{
	chanspec_t chanspec = 0;

	if (ch != 0) {
		/* Pass target channel information to avoid extra scan */
		join_params->params.chanspec_num = 1;
		join_params->params.chanspec_list[0] = ch;

		if (join_params->params.chanspec_list[0])
			chanspec |= WL_CHANSPEC_BAND_2G;
		else
			chanspec |= WL_CHANSPEC_BAND_5G;

		chanspec |= WL_CHANSPEC_BW_20;
		chanspec |= WL_CHANSPEC_CTL_SB_NONE;

		/* account for additional bssid and chanlist parameters */
		*join_params_size += WL_ASSOC_PARAMS_FIXED_SIZE +
			join_params->params.chanspec_num * sizeof(chanspec_t);

		/* update chanspec entries with target channel */
		join_params->params.chanspec_list[0]  &= WL_CHANSPEC_CHAN_MASK;
		join_params->params.chanspec_list[0] |= chanspec;
		join_params->params.chanspec_list[0] =
		        htodchanspec(join_params->params.chanspec_list[0]);

		join_params->params.chanspec_num = htod32(join_params->params.chanspec_num);

		WL_TRACE(("%s  join_params->params.chanspec_list[0]= %X\n",
			__FUNCTION__, join_params->params.chanspec_list[0]));
	}
	return 1;
}

static int
wl_iw_set_wap(
	struct net_device *dev,
	struct iw_request_info *info,
	struct sockaddr *awrq,
	char *extra
)
{
	int error = -EINVAL;
	wl_join_params_t join_params;
	int join_params_size;

	WL_TRACE(("%s: SIOCSIWAP\n", dev->name));

	if (awrq->sa_family != ARPHRD_ETHER) {
		WL_ERROR(("Invalid Header...sa_family\n"));
		return -EINVAL;
	}

	/* Ignore "auto" or "off" */
	if (ETHER_ISBCAST(awrq->sa_data) || ETHER_ISNULLADDR(awrq->sa_data)) {
		scb_val_t scbval;
		/* WL_ASSOC(("disassociating \n")); */
		bzero(&scbval, sizeof(scb_val_t));
		/* Ignore error (may be down or disassociated) */
		(void) dev_wlc_ioctl(dev, WLC_DISASSOC, &scbval, sizeof(scb_val_t));
		return 0;
	}


	/* Join with specific BSSID and cached SSID
	 *  If SSID is zero join based on BSSID only
	 */
	memset(&join_params, 0, sizeof(join_params));
	join_params_size = sizeof(join_params.ssid);

	memcpy(join_params.ssid.SSID, g_ssid.SSID, g_ssid.SSID_len);
	join_params.ssid.SSID_len = htod32(g_ssid.SSID_len);
	memcpy(&join_params.params.bssid, awrq->sa_data, ETHER_ADDR_LEN);

	/* join  to specific channel , use 0 for all  channels */
	/* g_wl_iw_params.target_channel = 0; */
	WL_TRACE(("%s  target_channel=%d\n", __FUNCTION__, g_wl_iw_params.target_channel));
	wl_iw_ch_to_chanspec(g_wl_iw_params.target_channel, &join_params, &join_params_size);

	if ((error = dev_wlc_ioctl(dev, WLC_SET_SSID, &join_params, join_params_size))) {
		WL_ERROR(("%s Invalid ioctl data=%d\n", __FUNCTION__, error));
		return error;
	}

	if (g_ssid.SSID_len) {
		WL_TRACE(("%s: join SSID=%s BSSID="MACSTR" ch=%d\n", __FUNCTION__,
			g_ssid.SSID, MAC2STR((u8 *)awrq->sa_data),
			g_wl_iw_params.target_channel));
	}

	/* Clean up cached SSID */
	memset(&g_ssid, 0, sizeof(g_ssid));
	return 0;
}

static int
wl_iw_get_wap(
	struct net_device *dev,
	struct iw_request_info *info,
	struct sockaddr *awrq,
	char *extra
)
{
	WL_TRACE(("%s: SIOCGIWAP\n", dev->name));

	awrq->sa_family = ARPHRD_ETHER;
	memset(awrq->sa_data, 0, ETHER_ADDR_LEN);

	/* Ignore error (may be down or disassociated) */
	(void) dev_wlc_ioctl(dev, WLC_GET_BSSID, awrq->sa_data, ETHER_ADDR_LEN);

	return 0;
}

#if WIRELESS_EXT > 17
static int
wl_iw_mlme(
	struct net_device *dev,
	struct iw_request_info *info,
	struct sockaddr *awrq,
	char *extra
)
{
	struct iw_mlme *mlme;
	scb_val_t scbval;
	int error  = -EINVAL;

	WL_TRACE(("%s: SIOCSIWMLME DISASSOC/DEAUTH\n", dev->name));

	mlme = (struct iw_mlme *)extra;
	if (mlme == NULL) {
		WL_ERROR(("Invalid ioctl data.\n"));
		return error;
	}

	scbval.val = mlme->reason_code;
	bcopy(&mlme->addr.sa_data, &scbval.ea, ETHER_ADDR_LEN);

	if (mlme->cmd == IW_MLME_DISASSOC) {
		scbval.val = htod32(scbval.val);
		error = dev_wlc_ioctl(dev, WLC_DISASSOC, &scbval, sizeof(scb_val_t));
	}
	else if (mlme->cmd == IW_MLME_DEAUTH) {
		scbval.val = htod32(scbval.val);
		error = dev_wlc_ioctl(dev, WLC_SCB_DEAUTHENTICATE_FOR_REASON, &scbval,
			sizeof(scb_val_t));
	}
	else {
		WL_ERROR(("Invalid ioctl data.\n"));
		return error;
	}

	return error;
}
#endif /* WIRELESS_EXT > 17 */

#ifndef WL_IW_USE_ISCAN
static int
wl_iw_get_aplist(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_point *dwrq,
	char *extra
)
{
	wl_scan_results_t *list;
	struct sockaddr *addr = (struct sockaddr *) extra;
	struct iw_quality qual[IW_MAX_AP];
	wl_bss_info_t *bi = NULL;
	int error, i;
	uint buflen = dwrq->length;

	WL_TRACE(("%s: SIOCGIWAPLIST\n", dev->name));

	if (!extra)
		return -EINVAL;

	/* Get scan results (too large to put on the stack) */
	list = kmalloc(buflen, GFP_KERNEL);
	if (!list)
		return -ENOMEM;
	memset(list, 0, buflen);
	list->buflen = htod32(buflen);
	if ((error = dev_wlc_ioctl(dev, WLC_SCAN_RESULTS, list, buflen))) {
		WL_ERROR(("%d: Scan results error %d\n", __LINE__, error));
		kfree(list);
		return error;
	}
	list->buflen = dtoh32(list->buflen);
	list->version = dtoh32(list->version);
	list->count = dtoh32(list->count);
	if (list->version != WL_BSS_INFO_VERSION) {
		WL_ERROR(("%s : list->version %d != WL_BSS_INFO_VERSION\n",
		          __FUNCTION__, list->version));
		kfree(list);
		return -EINVAL;
	}

	for (i = 0, dwrq->length = 0; i < list->count && dwrq->length < IW_MAX_AP; i++) {
		bi = bi ? (wl_bss_info_t *)((uintptr)bi + dtoh32(bi->length)) : list->bss_info;
		ASSERT(((uintptr)bi + dtoh32(bi->length)) <= ((uintptr)list +
			buflen));

		/* Infrastructure only */
		if (!(dtoh16(bi->capability) & DOT11_CAP_ESS))
			continue;

		/* BSSID */
		memcpy(addr[dwrq->length].sa_data, &bi->BSSID, ETHER_ADDR_LEN);
		addr[dwrq->length].sa_family = ARPHRD_ETHER;
		qual[dwrq->length].qual = rssi_to_qual(dtoh16(bi->RSSI));
		qual[dwrq->length].level = 0x100 + dtoh16(bi->RSSI);
		qual[dwrq->length].noise = 0x100 + bi->phy_noise;

		/* Updated qual, level, and noise */
#if WIRELESS_EXT > 18
		qual[dwrq->length].updated = IW_QUAL_ALL_UPDATED | IW_QUAL_DBM;
#else
		qual[dwrq->length].updated = 7;
#endif /* WIRELESS_EXT > 18 */

		dwrq->length++;
	}

	kfree(list);

	if (dwrq->length) {
		memcpy(&addr[dwrq->length], qual, sizeof(struct iw_quality) * dwrq->length);
		/* Provided qual */
		dwrq->flags = 1;
	}

	return 0;
}
#endif /* WL_IW_USE_ISCAN */

#ifdef WL_IW_USE_ISCAN
static int
wl_iw_iscan_get_aplist(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_point *dwrq,
	char *extra
)
{
	wl_scan_results_t *list;
	iscan_buf_t * buf;
	iscan_info_t *iscan = g_iscan;

	struct sockaddr *addr = (struct sockaddr *) extra;
	struct iw_quality qual[IW_MAX_AP];
	wl_bss_info_t *bi = NULL;
	int i;

	WL_TRACE(("%s: SIOCGIWAPLIST\n", dev->name));

	if (!extra)
		return -EINVAL;

	if ((!iscan) || (iscan->sysioc_pid < 0)) {
		WL_ERROR(("%s error\n", __FUNCTION__));
		return 0;
	}

	buf = iscan->list_hdr;
	/* Get scan results (too large to put on the stack) */
	while (buf) {
		list = &((wl_iscan_results_t*)buf->iscan_buf)->results;
		if (list->version != WL_BSS_INFO_VERSION) {
			WL_ERROR(("%s : list->version %d != WL_BSS_INFO_VERSION\n",
				__FUNCTION__, list->version));
			return -EINVAL;
		}

		bi = NULL;
		for (i = 0, dwrq->length = 0; i < list->count && dwrq->length < IW_MAX_AP; i++) {
			bi = bi ? (wl_bss_info_t *)((uintptr)bi + dtoh32(bi->length))
			          : list->bss_info;
			ASSERT(((uintptr)bi + dtoh32(bi->length)) <= ((uintptr)list +
				WLC_IW_ISCAN_MAXLEN));

			/* Infrastructure only */
			if (!(dtoh16(bi->capability) & DOT11_CAP_ESS))
				continue;

			/* BSSID */
			memcpy(addr[dwrq->length].sa_data, &bi->BSSID, ETHER_ADDR_LEN);
			addr[dwrq->length].sa_family = ARPHRD_ETHER;
			qual[dwrq->length].qual = rssi_to_qual(dtoh16(bi->RSSI));
			qual[dwrq->length].level = 0x100 + dtoh16(bi->RSSI);
			qual[dwrq->length].noise = 0x100 + bi->phy_noise;

			/* Updated qual, level, and noise */
#if WIRELESS_EXT > 18
			qual[dwrq->length].updated = IW_QUAL_ALL_UPDATED | IW_QUAL_DBM;
#else
			qual[dwrq->length].updated = 7;
#endif /* WIRELESS_EXT > 18 */

			dwrq->length++;
		}
		buf = buf->next;
	}
	if (dwrq->length) {
		memcpy(&addr[dwrq->length], qual, sizeof(struct iw_quality) * dwrq->length);
		/* Provided qual */
		dwrq->flags = 1;
	}

	return 0;
}

static int
wl_iw_iscan_prep(wl_scan_params_t *params, wlc_ssid_t *ssid)
{
	int err = 0;

	memcpy(&params->bssid, &ether_bcast, ETHER_ADDR_LEN);
	params->bss_type = DOT11_BSSTYPE_ANY;
	params->scan_type = 0;
	params->nprobes = -1;
	params->active_time = -1;
	params->passive_time = -1;
	params->home_time = -1;
	params->channel_num = 0;

	params->nprobes = htod32(params->nprobes);
	params->active_time = htod32(params->active_time);
	params->passive_time = htod32(params->passive_time);
	params->home_time = htod32(params->home_time);
	if (ssid && ssid->SSID_len)
		memcpy(&params->ssid, ssid, sizeof(wlc_ssid_t));

	return err;
}

static int
wl_iw_iscan(iscan_info_t *iscan, wlc_ssid_t *ssid, uint16 action)
{
	int err = 0;

	iscan->iscan_ex_params_p->version = htod32(ISCAN_REQ_VERSION);
	iscan->iscan_ex_params_p->action = htod16(action);
	iscan->iscan_ex_params_p->scan_duration = htod16(0);

	WL_SCAN(("%s : nprobes=%d\n", __FUNCTION__, iscan->iscan_ex_params_p->params.nprobes));
	WL_SCAN(("active_time=%d\n", iscan->iscan_ex_params_p->params.active_time));
	WL_SCAN(("passive_time=%d\n", iscan->iscan_ex_params_p->params.passive_time));
	WL_SCAN(("home_time=%d\n", iscan->iscan_ex_params_p->params.home_time));
	WL_SCAN(("scan_type=%d\n", iscan->iscan_ex_params_p->params.scan_type));
	WL_SCAN(("bss_type=%d\n", iscan->iscan_ex_params_p->params.bss_type));

	if ((dev_iw_iovar_setbuf(iscan->dev, "iscan", iscan->iscan_ex_params_p,
		iscan->iscan_ex_param_size, iscan->ioctlbuf, sizeof(iscan->ioctlbuf)))) {
			WL_ERROR(("Set ISCAN for %s failed with %d\n", __FUNCTION__, err));
			err = -1;
	}

	return err;
}

static void
wl_iw_timerfunc(ulong data)
{
	iscan_info_t *iscan = (iscan_info_t *)data;
	if (iscan) {
		iscan->timer_on = 0;
		if (iscan->iscan_state != ISCAN_STATE_IDLE) {
			WL_TRACE(("timer trigger\n"));
			up(&iscan->sysioc_sem);
		}
	}
}

static void
wl_iw_set_event_mask(struct net_device *dev)
{
	char eventmask[WL_EVENTING_MASK_LEN];
	char iovbuf[WL_EVENTING_MASK_LEN + 12];	/* Room for "event_msgs" + '\0' + bitvec */

	dev_iw_iovar_getbuf(dev, "event_msgs", "", 0, iovbuf, sizeof(iovbuf));
	bcopy(iovbuf, eventmask, WL_EVENTING_MASK_LEN);
	setbit(eventmask, WLC_E_SCAN_COMPLETE);
	dev_iw_iovar_setbuf(dev, "event_msgs", eventmask, WL_EVENTING_MASK_LEN,
		iovbuf, sizeof(iovbuf));
}

static uint32
wl_iw_iscan_get(iscan_info_t *iscan)
{
	iscan_buf_t * buf;
	iscan_buf_t * ptr;
	wl_iscan_results_t * list_buf;
	wl_iscan_results_t list;
	wl_scan_results_t *results;
	uint32 status;
	int res = 0;

	DHD_OS_MUTEX_LOCK(&wl_cache_lock);
	if (iscan->list_cur) {
		buf = iscan->list_cur;
		iscan->list_cur = buf->next;
	}
	else {
		buf = kmalloc(sizeof(iscan_buf_t), GFP_KERNEL);
		if (!buf) {
			WL_ERROR(("%s can't alloc iscan_buf_t : going to abort currect iscan\n",
			          __FUNCTION__));
			DHD_OS_MUTEX_UNLOCK(&wl_cache_lock);
			return WL_SCAN_RESULTS_NO_MEM;
		}
		buf->next = NULL;
		if (!iscan->list_hdr)
			iscan->list_hdr = buf;
		else {
			ptr = iscan->list_hdr;
			while (ptr->next) {
				ptr = ptr->next;
			}
			ptr->next = buf;
		}
	}
	memset(buf->iscan_buf, 0, WLC_IW_ISCAN_MAXLEN);
	list_buf = (wl_iscan_results_t*)buf->iscan_buf;
	results = &list_buf->results;
	results->buflen = WL_ISCAN_RESULTS_FIXED_SIZE;
	results->version = 0;
	results->count = 0;

	memset(&list, 0, sizeof(list));
	list.results.buflen = htod32(WLC_IW_ISCAN_MAXLEN);
	res = dev_iw_iovar_getbuf(
		iscan->dev,
		"iscanresults",
		&list,
		WL_ISCAN_RESULTS_FIXED_SIZE,
		buf->iscan_buf,
		WLC_IW_ISCAN_MAXLEN);
	if (res == 0) {
		results->buflen = dtoh32(results->buflen);
		results->version = dtoh32(results->version);
		results->count = dtoh32(results->count);
		WL_TRACE(("results->count = %d\n", results->count));
		WL_TRACE(("results->buflen = %d\n", results->buflen));
		status = dtoh32(list_buf->status);
	} else {
		WL_ERROR(("%s returns error %d\n", __FUNCTION__, res));
		/* TODO Add ISCAN abort call */
		status = WL_SCAN_RESULTS_NO_MEM;
	}
	DHD_OS_MUTEX_UNLOCK(&wl_cache_lock);
	return status;
}

static void
wl_iw_force_specific_scan(iscan_info_t *iscan)
{
	WL_TRACE(("%s force Specific SCAN for %s\n", __FUNCTION__, g_specific_ssid.SSID));
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
	rtnl_lock();
#endif

	(void) dev_wlc_ioctl(iscan->dev, WLC_SCAN, &g_specific_ssid, sizeof(g_specific_ssid));

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
	rtnl_unlock();
#endif
}

static void
wl_iw_send_scan_complete(iscan_info_t *iscan)
{
#ifndef SANDGATE2G
	union iwreq_data wrqu;

	memset(&wrqu, 0, sizeof(wrqu));

	/* wext expects to get no data for SIOCGIWSCAN Event  */
	wireless_send_event(iscan->dev, SIOCGIWSCAN, &wrqu, NULL);
		if (g_first_broadcast_scan == BROADCAST_SCAN_FIRST_STARTED)
			g_first_broadcast_scan = BROADCAST_SCAN_FIRST_RESULT_READY;
		WL_TRACE(("Send Event ISCAN complete\n"));
#endif /* SANDGATE2G */
}

static int
_iscan_sysioc_thread(void *data)
{
	uint32 status;
	iscan_info_t *iscan = (iscan_info_t *)data;
	static bool iscan_pass_abort = FALSE;

	DAEMONIZE("iscan_sysioc");

	status = WL_SCAN_RESULTS_PARTIAL;
	while (down_interruptible(&iscan->sysioc_sem) == 0) {

	net_os_wake_lock(iscan->dev);

#if defined(SOFTAP)
		/* NO scan operations in AP mode  */
		if (ap_cfg_running) {
		 WL_TRACE(("%s skipping SCAN ops in AP mode !!!\n", __FUNCTION__));
		 net_os_wake_unlock(iscan->dev);
		 continue;
		}
#endif /* SOFTAP */

		if (iscan->timer_on) {
			/* to prevent the timer from being deleted twice (concurrent case)
			 * as all code checks timer_on before deleting it, maybe a spin
			 * lock is needed.
			 */
			iscan->timer_on = 0;
			del_timer_sync(&iscan->timer);
		}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
		rtnl_lock();
#endif
		status = wl_iw_iscan_get(iscan);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
		rtnl_unlock();
#endif

	if  (g_scan_specified_ssid && (iscan_pass_abort == TRUE)) {
		WL_TRACE(("%s Get results from specific scan status=%d\n", __FUNCTION__, status));
			wl_iw_send_scan_complete(iscan);
			iscan_pass_abort = FALSE;
			status  = -1;
		}

		switch (status) {
			case WL_SCAN_RESULTS_PARTIAL:
				WL_TRACE(("iscanresults incomplete\n"));
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
				rtnl_lock();
#endif
				/* make sure our buffer size is enough before going next round */
				wl_iw_iscan(iscan, NULL, WL_SCAN_ACTION_CONTINUE);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
				rtnl_unlock();
#endif
				/* Reschedule the timer */
				mod_timer(&iscan->timer, jiffies + iscan->timer_ms*HZ/1000);
				iscan->timer_on = 1;
				break;
			case WL_SCAN_RESULTS_SUCCESS:
				WL_TRACE(("iscanresults complete\n"));
				iscan->iscan_state = ISCAN_STATE_IDLE;
				wl_iw_send_scan_complete(iscan);
				break;
			case WL_SCAN_RESULTS_PENDING:
				WL_TRACE(("iscanresults pending\n"));
				/* Reschedule the timer */
				mod_timer(&iscan->timer, jiffies + iscan->timer_ms*HZ/1000);
				iscan->timer_on = 1;
				break;
			case WL_SCAN_RESULTS_ABORTED:
				WL_TRACE(("iscanresults aborted\n"));
				iscan->iscan_state = ISCAN_STATE_IDLE;
				if (g_scan_specified_ssid == 0)
					wl_iw_send_scan_complete(iscan);
				else {
					iscan_pass_abort = TRUE;
					wl_iw_force_specific_scan(iscan);
				}
				break;
			case WL_SCAN_RESULTS_NO_MEM:
				WL_TRACE(("iscanresults can't alloc memory: skip\n"));
				iscan->iscan_state = ISCAN_STATE_IDLE;
				break;
			default:
				WL_TRACE(("iscanresults returned unknown status %d\n", status));
				break;
		 }

		net_os_wake_unlock(iscan->dev);
	}

	if (iscan->timer_on) {
		iscan->timer_on = 0;
		del_timer_sync(&iscan->timer);
	}

	complete_and_exit(&iscan->sysioc_exited, 0);
}
#endif /* #ifndef WL_IW_USE_ISCAN */

#if !defined(CSCAN)

static void
wl_iw_set_ss_cache_timer_flag(void)
{
	g_ss_cache_ctrl.m_timer_expired = 1;
	WL_TRACE(("%s called\n", __FUNCTION__));
}

/* initialize spec scan cache controller */
static int
wl_iw_init_ss_cache_ctrl(void)
{
	WL_TRACE(("%s :\n", __FUNCTION__));
	g_ss_cache_ctrl.m_prev_scan_mode = 0;
	g_ss_cache_ctrl.m_cons_br_scan_cnt = 0;
	g_ss_cache_ctrl.m_cache_head = NULL;
	g_ss_cache_ctrl.m_link_down = 0;
	g_ss_cache_ctrl.m_timer_expired = 0;
	memset(g_ss_cache_ctrl.m_active_bssid, 0, ETHER_ADDR_LEN);

	g_ss_cache_ctrl.m_timer = kmalloc(sizeof(struct timer_list), GFP_KERNEL);
	if (!g_ss_cache_ctrl.m_timer) {
		return -ENOMEM;
	}
	g_ss_cache_ctrl.m_timer->function = (void *)wl_iw_set_ss_cache_timer_flag;
	init_timer(g_ss_cache_ctrl.m_timer);

	return 0;
}


/*
* free all dynamic allocated resources for specific scan cache
*/
static void
wl_iw_free_ss_cache(void)
{
	wl_iw_ss_cache_t *node, *cur;
	wl_iw_ss_cache_t **spec_scan_head;

	WL_TRACE(("%s called\n", __FUNCTION__));

	DHD_OS_MUTEX_LOCK(&wl_cache_lock);
	spec_scan_head = &g_ss_cache_ctrl.m_cache_head;
	node = *spec_scan_head;

	for (;node;) {
		WL_TRACE(("%s : SSID - %s\n", __FUNCTION__, node->bss_info->SSID));
		cur = node;
		node = cur->next;
		kfree(cur);
	}
	*spec_scan_head = NULL;
	DHD_OS_MUTEX_UNLOCK(&wl_cache_lock);
}


/*
* run spec cache timer
* kick_off : 1 - timer start
* kick_off : 0 - timer stop
*/
static int
wl_iw_run_ss_cache_timer(int kick_off)
{
	struct timer_list **timer;

	timer = &g_ss_cache_ctrl.m_timer;

	if (*timer) {
		if (kick_off) {
#ifdef CONFIG_PRESCANNED
			(*timer)->expires = jiffies + 70000 * HZ / 1000;
#else
			(*timer)->expires = jiffies + 30000 * HZ / 1000;	/* 30 sec timer */
#endif
			add_timer(*timer);
			WL_TRACE(("%s : timer starts \n", __FUNCTION__));
		} else {
			del_timer_sync(*timer);
			WL_TRACE(("%s : timer stops \n", __FUNCTION__));
		}
	}

	return 0;
}

/* release spec cache resources */
static void
wl_iw_release_ss_cache_ctrl(void)
{
	WL_TRACE(("%s :\n", __FUNCTION__));
	wl_iw_free_ss_cache();
	wl_iw_run_ss_cache_timer(0);
	if (g_ss_cache_ctrl.m_timer) {
		kfree(g_ss_cache_ctrl.m_timer);
	}
}


/*
* check if there is any specific scaned AP which was not updated in previous round
* if it is not updated, and then it is removed from specific scan cache
*/
static void
wl_iw_reset_ss_cache(void)
{
	wl_iw_ss_cache_t *node, *prev, *cur;
	wl_iw_ss_cache_t **spec_scan_head;

	DHD_OS_MUTEX_LOCK(&wl_cache_lock);
	spec_scan_head = &g_ss_cache_ctrl.m_cache_head;
	node = *spec_scan_head;
	prev = node;

	for (;node;) {
		WL_TRACE(("%s : node SSID %s \n", __FUNCTION__, node->bss_info->SSID));
		if (!node->dirty) {
			cur = node;
			if (cur == *spec_scan_head) {
				*spec_scan_head = cur->next;
				prev = *spec_scan_head;
			}
			else {
				prev->next = cur->next;
			}
			node = cur->next;

			WL_TRACE(("%s : Del node : SSID %s\n", __FUNCTION__, cur->bss_info->SSID));
			kfree(cur);
			continue;
		}

		node->dirty = 0;
		prev = node;
		node = node->next;
	}
	DHD_OS_MUTEX_UNLOCK(&wl_cache_lock);
}

/*
* add specific scanned AP to specific scan cache
*/
static int
wl_iw_add_bss_to_ss_cache(wl_scan_results_t *ss_list)
{

	wl_iw_ss_cache_t *node, *prev, *leaf;
	wl_iw_ss_cache_t **spec_scan_head;
	wl_bss_info_t *bi = NULL;
	int i;

	/* ASSERT(ss_list->version == WL_BSS_INFO_VERSION); */
	if (!ss_list->count) {
		return 0;
	}

	DHD_OS_MUTEX_LOCK(&wl_cache_lock);
	spec_scan_head = &g_ss_cache_ctrl.m_cache_head;

	for (i = 0; i < ss_list->count; i++) {

		node = *spec_scan_head;
		prev = node;

		bi = bi ? (wl_bss_info_t *)((uintptr)bi + dtoh32(bi->length)) : ss_list->bss_info;

		WL_TRACE(("%s : find %d with specific SSID %s\n", __FUNCTION__, i, bi->SSID));
		for (;node;) {
			if (!memcmp(&node->bss_info->BSSID, &bi->BSSID, ETHER_ADDR_LEN)) {
				/*
				* if this AP is already in the cache,
				* and then just marks dirty field and returns
				*/
				WL_TRACE(("dirty marked : SSID %s\n", bi->SSID));
				node->dirty = 1;
				break;
			}
			prev = node;
			node = node->next;
		}

		if (node) {
			continue;
		}

		leaf = kmalloc(bi->length + WLC_IW_SS_CACHE_CTRL_FIELD_MAXLEN, GFP_KERNEL);
		if (!leaf) {
			WL_ERROR(("Memory alloc failure %d\n",
				bi->length + WLC_IW_SS_CACHE_CTRL_FIELD_MAXLEN));
			DHD_OS_MUTEX_UNLOCK(&wl_cache_lock);
			return -ENOMEM;
		}

		memcpy(leaf->bss_info, bi, bi->length);
		leaf->next = NULL;
		leaf->dirty = 1;
		leaf->count = 1;
		leaf->version = ss_list->version;

		if (!prev) {
			*spec_scan_head = leaf;
		}
		else {
			prev->next = leaf;
		}
	}
	DHD_OS_MUTEX_UNLOCK(&wl_cache_lock);
	return 0;
}

/*
* merge specific scan cache into entire scan result cache
*/
static int
wl_iw_merge_scan_cache(struct iw_request_info *info, char *extra, uint buflen_from_user,
__u16 *merged_len)
{
	wl_iw_ss_cache_t *node;
	wl_scan_results_t *list_merge;

	DHD_OS_MUTEX_LOCK(&wl_cache_lock);
	node = g_ss_cache_ctrl.m_cache_head;
	for (;node;) {
		list_merge = (wl_scan_results_t *)node;
		WL_TRACE(("%s: Cached Specific APs list=%d\n", __FUNCTION__, list_merge->count));
		if (buflen_from_user - *merged_len > 0) {
			*merged_len += (__u16) wl_iw_get_scan_prep(list_merge, info,
				extra + *merged_len, buflen_from_user - *merged_len);
		}
		else {
			WL_TRACE(("%s: exit with break\n", __FUNCTION__));
			break;
		}
		node = node->next;
	}
	DHD_OS_MUTEX_UNLOCK(&wl_cache_lock);
	return 0;
}

/*
* delete disappeared AP from specific scan cache
*/
static int
wl_iw_delete_bss_from_ss_cache(void *addr)
{

	wl_iw_ss_cache_t *node, *prev;
	wl_iw_ss_cache_t **spec_scan_head;

	DHD_OS_MUTEX_LOCK(&wl_cache_lock);
	spec_scan_head = &g_ss_cache_ctrl.m_cache_head;
	node = *spec_scan_head;
	prev = node;
	for (;node;) {
		if (!memcmp(&node->bss_info->BSSID, addr, ETHER_ADDR_LEN)) {
			if (node == *spec_scan_head) {
				*spec_scan_head = node->next;
			}
			else {
				prev->next = node->next;
			}

			WL_TRACE(("%s : Del node : %s\n", __FUNCTION__, node->bss_info->SSID));
			kfree(node);
			break;
		}

		prev = node;
		node = node->next;
	}

	memset(addr, 0, ETHER_ADDR_LEN);
	DHD_OS_MUTEX_UNLOCK(&wl_cache_lock);
	return 0;
}

#endif	

static int
wl_iw_set_scan(
	struct net_device *dev,
	struct iw_request_info *info,
	union iwreq_data *wrqu,
	char *extra
)
{
	int error;
	WL_TRACE(("\n:%s dev:%s: SIOCSIWSCAN : SCAN\n", __FUNCTION__, dev->name));

#ifdef OEM_CHROMIUMOS
	g_set_essid_before_scan = FALSE;
#endif

#if defined(CSCAN)
		WL_ERROR(("%s: Scan from SIOCGIWSCAN not supported\n", __FUNCTION__));
		return -EINVAL;
#endif /* defined(CSCAN) */

#if defined(SOFTAP)
	/*  IN SOFTAP mode , scan must never be performed  */
	if (ap_cfg_running) {
		WL_TRACE(("\n>%s: Not executed, reason -'SOFTAP is active'\n", __FUNCTION__));
		return 0;
	}
#endif 

	/* Android may send scan request even before START if finished
	     Ignoring scan request when chip is still off
	*/
	if (g_onoff == G_WLAN_SET_OFF)
		return 0;

	/* Broadcast scan */
	memset(&g_specific_ssid, 0, sizeof(g_specific_ssid));
#ifndef WL_IW_USE_ISCAN
	/* with ISCAN scan used only for specific scan */
	g_scan_specified_ssid = 0;
#endif /* WL_IW_USE_ISCAN */

#if WIRELESS_EXT > 17
	/* check for given essid */
	if (wrqu->data.length == sizeof(struct iw_scan_req)) {
		if (wrqu->data.flags & IW_SCAN_THIS_ESSID) {
			struct iw_scan_req *req = (struct iw_scan_req *)extra;
			if (g_first_broadcast_scan != BROADCAST_SCAN_FIRST_RESULT_CONSUMED) {
				/* TODO : queue SP scan and restart specific scan later */
				WL_TRACE(("%s Ignoring SC %s first BC is not done = %d\n",
				          __FUNCTION__, req->essid,
				          g_first_broadcast_scan));
				return -EBUSY;
			}
			if (g_scan_specified_ssid) {
				WL_TRACE(("%s Specific SCAN is not done ignore scan for = %s \n",
					__FUNCTION__, req->essid));
				/* TODO : queue SP scan and restart specific scan later */
				return -EBUSY;
			}
			else {
				g_specific_ssid.SSID_len = MIN(sizeof(g_specific_ssid.SSID),
				                               req->essid_len);
				memcpy(g_specific_ssid.SSID, req->essid, g_specific_ssid.SSID_len);
				g_specific_ssid.SSID_len = htod32(g_specific_ssid.SSID_len);
				g_scan_specified_ssid = 1;
				WL_TRACE(("### Specific scan ssid=%s len=%d\n",
				          g_specific_ssid.SSID, g_specific_ssid.SSID_len));
			}
		}
	}
#endif /* WIRELESS_EXT > 17 */
	/* Ignore error (most likely scan in progress) */
	if ((error = dev_wlc_ioctl(dev, WLC_SCAN, &g_specific_ssid, sizeof(g_specific_ssid)))) {
		WL_TRACE(("#### Set SCAN for %s failed with %d\n", g_specific_ssid.SSID, error));
		/* if scan failed clean g_scan_specified_ssid flag */
		g_scan_specified_ssid = 0;
		return -EBUSY;
	}

	return 0;
}

#ifdef WL_IW_USE_ISCAN
int
wl_iw_iscan_set_scan_broadcast_prep(struct net_device *dev, uint flag)
{
	wlc_ssid_t ssid;
	iscan_info_t *iscan = g_iscan;

	/* force first broadcat scan */
	if (g_first_broadcast_scan == BROADCAST_SCAN_FIRST_IDLE) {
		g_first_broadcast_scan = BROADCAST_SCAN_FIRST_STARTED;
		WL_TRACE(("%s: First Brodcast scan was forced\n", __FUNCTION__));
	}
	else if (g_first_broadcast_scan == BROADCAST_SCAN_FIRST_STARTED) {
		WL_TRACE(("%s: ignore ISCAN request first BS is not done yet\n", __FUNCTION__));
		return 0;
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
	if (flag)
		rtnl_lock();
#endif

	dev_wlc_ioctl(dev, WLC_SET_PASSIVE_SCAN, &iscan->scan_flag, sizeof(iscan->scan_flag));
	wl_iw_set_event_mask(dev);

	WL_TRACE(("+++: Set Broadcast ISCAN\n"));
	/* default Broadcast scan */
	memset(&ssid, 0, sizeof(ssid));

	iscan->list_cur = iscan->list_hdr;
	iscan->iscan_state = ISCAN_STATE_SCANING;

	memset(&iscan->iscan_ex_params_p->params, 0, iscan->iscan_ex_param_size);
	wl_iw_iscan_prep(&iscan->iscan_ex_params_p->params, &ssid);
	wl_iw_iscan(iscan, &ssid, WL_SCAN_ACTION_START);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
	if (flag)
		rtnl_unlock();
#endif

	mod_timer(&iscan->timer, jiffies + iscan->timer_ms*HZ/1000);

	iscan->timer_on = 1;

	return 0;
}

static int
wl_iw_iscan_set_scan(
	struct net_device *dev,
	struct iw_request_info *info,
	union iwreq_data *wrqu,
	char *extra
)
{
	wlc_ssid_t ssid;
	iscan_info_t *iscan = g_iscan;
	int ret = 0; /* Lin - for releasing wake lock properly */

	WL_TRACE(("%s: SIOCSIWSCAN : ISCAN\n", dev->name));

#if defined(CSCAN)
		WL_ERROR(("%s: Scan from SIOCGIWSCAN not supported\n", __FUNCTION__));
		return -EINVAL;
#endif /* defined(CSCAN) */

	net_os_wake_lock(dev);

	/* IN SOFTAP mode , scan must never be performed  */
#if defined(SOFTAP)
	if (ap_cfg_running) {
		WL_TRACE(("\n>%s: Not executed, reason -'SOFTAP is active'\n", __FUNCTION__));
		goto set_scan_end;
	}
#endif
	/* Android may send scan request even before START if finished
	     Ignoring scan request when chip is still off
	*/
	if (g_onoff == G_WLAN_SET_OFF) {
		WL_TRACE(("%s: driver is not up yet after START\n", __FUNCTION__));
		goto set_scan_end;
	}

#ifdef PNO_SUPPORT
	/* maybe better ignore scan request when PNO still active */
	if  (dhd_dev_get_pno_status(dev)) {
		WL_ERROR(("%s: Scan called when PNO is active\n", __FUNCTION__));
	}
#endif /* PNO_SUPPORT */

	/* use backup if our thread is not successful */
	if ((!iscan) || (iscan->sysioc_pid < 0)) {
		WL_ERROR(("%s error \n",  __FUNCTION__));
		goto set_scan_end;
	}

	if (g_scan_specified_ssid) {
		WL_TRACE(("%s Specific SCAN already running ignoring BC scan\n",
		          __FUNCTION__));
		ret = EBUSY;
		goto set_scan_end;
	}

	/* clean Broadcast scan */
	memset(&ssid, 0, sizeof(ssid));

#if WIRELESS_EXT > 17
	/* check for given essid */
	if (wrqu->data.length == sizeof(struct iw_scan_req)) {
		if (wrqu->data.flags & IW_SCAN_THIS_ESSID) {
			int as = 0;
			struct iw_scan_req *req = (struct iw_scan_req *)extra;
			/* moved to the end, see down below */
			ssid.SSID_len = MIN(sizeof(ssid.SSID), req->essid_len);
			memcpy(ssid.SSID, req->essid, ssid.SSID_len);
			ssid.SSID_len = htod32(ssid.SSID_len);
			dev_wlc_ioctl(dev, WLC_SET_PASSIVE_SCAN, &as, sizeof(as));
			wl_iw_set_event_mask(dev);
			ret = wl_iw_set_scan(dev, info, wrqu, extra);
			goto set_scan_end;
		}
		else {
			g_scan_specified_ssid = 0;

			if (iscan->iscan_state == ISCAN_STATE_SCANING) {
				WL_TRACE(("%s ISCAN already in progress \n", __FUNCTION__));
				goto set_scan_end;
			}
		}
	}
#endif /* WIRELESS_EXT > 17 */

#if !defined(CSCAN)
	if (g_first_broadcast_scan < BROADCAST_SCAN_FIRST_RESULT_CONSUMED) {
		if (++g_first_counter_scans == MAX_ALLOWED_BLOCK_SCAN_FROM_FIRST_SCAN) {

			WL_ERROR(("%s Clean up First scan flag which is %d\n",
			          __FUNCTION__, g_first_broadcast_scan));
			g_first_broadcast_scan = BROADCAST_SCAN_FIRST_RESULT_CONSUMED;
		}
		else {
			WL_ERROR(("%s Ignoring Broadcast Scan:First Scan is not done yet %d\n",
			          __FUNCTION__, g_first_counter_scans));
			ret = -EBUSY;
			goto set_scan_end;
		}
	}
#endif

	wl_iw_iscan_set_scan_broadcast_prep(dev, 0);

set_scan_end:
	net_os_wake_unlock(dev);
	return ret;
}
#endif /* WL_IW_USE_ISCAN */

#if WIRELESS_EXT > 17
static bool
ie_is_wpa_ie(uint8 **wpaie, uint8 **tlvs, int *tlvs_len)
{
/* Is this body of this tlvs entry a WPA entry? If */
/* not update the tlvs buffer pointer/length */
	uint8 *ie = *wpaie;

	/* If the contents match the WPA_OUI and type=1 */
	if ((ie[1] >= 6) &&
		!bcmp((const void *)&ie[2], (const void *)(WPA_OUI "\x01"), 4)) {
		return TRUE;
	}

	/* point to the next ie */
	ie += ie[1] + 2;
	/* calculate the length of the rest of the buffer */
	*tlvs_len -= (int)(ie - *tlvs);
	/* update the pointer to the start of the buffer */
	*tlvs = ie;
	return FALSE;
}

static bool
ie_is_wps_ie(uint8 **wpsie, uint8 **tlvs, int *tlvs_len)
{
/* Is this body of this tlvs entry a WPS entry? If */
/* not update the tlvs buffer pointer/length */
	uint8 *ie = *wpsie;

	/* If the contents match the WPA_OUI and type=4 */
	if ((ie[1] >= 4) &&
		!bcmp((const void *)&ie[2], (const void *)(WPA_OUI "\x04"), 4)) {
		return TRUE;
	}

	/* point to the next ie */
	ie += ie[1] + 2;
	/* calculate the length of the rest of the buffer */
	*tlvs_len -= (int)(ie - *tlvs);
	/* update the pointer to the start of the buffer */
	*tlvs = ie;
	return FALSE;
}
#endif /* WIRELESS_EXT > 17 */


static int
wl_iw_handle_scanresults_ies(char **event_p, char *end,
	struct iw_request_info *info, wl_bss_info_t *bi)
{
#if WIRELESS_EXT > 17
	struct iw_event	iwe;
	char *event;

	event = *event_p;
	if (bi->ie_length) {
		/* look for wpa/rsn ies in the ie list... */
		bcm_tlv_t *ie;
		uint8 *ptr = ((uint8 *)bi) + sizeof(wl_bss_info_t);
		int ptr_len = bi->ie_length;

		if ((ie = bcm_parse_tlvs(ptr, ptr_len, DOT11_MNG_RSN_ID))) {
			iwe.cmd = IWEVGENIE;
			iwe.u.data.length = ie->len + 2;
			event = IWE_STREAM_ADD_POINT(info, event, end, &iwe, (char *)ie);
		}
		ptr = ((uint8 *)bi) + sizeof(wl_bss_info_t);

		while ((ie = bcm_parse_tlvs(ptr, ptr_len, DOT11_MNG_WPA_ID))) {
			/* look for WPS IE */
			if (ie_is_wps_ie(((uint8 **)&ie), &ptr, &ptr_len)) {
				iwe.cmd = IWEVGENIE;
				iwe.u.data.length = ie->len + 2;
				event = IWE_STREAM_ADD_POINT(info, event, end, &iwe, (char *)ie);
				break;
			}
		}

		ptr = ((uint8 *)bi) + sizeof(wl_bss_info_t);
		ptr_len = bi->ie_length;
		while ((ie = bcm_parse_tlvs(ptr, ptr_len, DOT11_MNG_WPA_ID))) {
			if (ie_is_wpa_ie(((uint8 **)&ie), &ptr, &ptr_len)) {
				iwe.cmd = IWEVGENIE;
				iwe.u.data.length = ie->len + 2;
				event = IWE_STREAM_ADD_POINT(info, event, end, &iwe, (char *)ie);
				break;
			}
		}

	*event_p = event;
	}
#endif /* WIRELESS_EXT > 17 */

	return 0;
}

static uint
wl_iw_get_scan_prep(
	wl_scan_results_t *list,
	struct iw_request_info *info,
	char *extra,
	short max_size)
{
	int  i, j;
	struct iw_event  iwe;
	wl_bss_info_t *bi = NULL;
	char *event = extra, *end = extra + max_size - WE_ADD_EVENT_FIX, *value;
	int	ret = 0;

	ASSERT(list);

	/* End pointer (*end) for any IWE_STREAM_ADD_EVENT should be
	 * corrected by WE_ADD_EVENT_FIX because kernel iw_handler is
	 * checking end round up to specific event_len fields only
	 */

	for (i = 0; i < list->count && i < IW_MAX_AP; i++) {
		if (list->version != WL_BSS_INFO_VERSION) {
			WL_ERROR(("%s : list->version %d != WL_BSS_INFO_VERSION\n",
			          __FUNCTION__, list->version));
			return ret;
		}

		bi = bi ? (wl_bss_info_t *)((uintptr)bi + dtoh32(bi->length)) : list->bss_info;

		WL_TRACE(("%s : %s\n", __FUNCTION__, bi->SSID));

		/* First entry must be the BSSID */
		iwe.cmd = SIOCGIWAP;
		iwe.u.ap_addr.sa_family = ARPHRD_ETHER;
		memcpy(iwe.u.ap_addr.sa_data, &bi->BSSID, ETHER_ADDR_LEN);
		event = IWE_STREAM_ADD_EVENT(info, event, end, &iwe, IW_EV_ADDR_LEN);
		/* SSID */
		iwe.u.data.length = dtoh32(bi->SSID_len);
		iwe.cmd = SIOCGIWESSID;
		iwe.u.data.flags = 1;
		event = IWE_STREAM_ADD_POINT(info, event, end, &iwe, bi->SSID);

		/* Mode */
		if (dtoh16(bi->capability) & (DOT11_CAP_ESS | DOT11_CAP_IBSS)) {
			iwe.cmd = SIOCGIWMODE;
			if (dtoh16(bi->capability) & DOT11_CAP_ESS)
				iwe.u.mode = IW_MODE_INFRA;
			else
				iwe.u.mode = IW_MODE_ADHOC;
			event = IWE_STREAM_ADD_EVENT(info, event, end, &iwe, IW_EV_UINT_LEN);
		}

		/* Channel */
		iwe.cmd = SIOCGIWFREQ;
		iwe.u.freq.m = wf_channel2mhz(CHSPEC_CHANNEL(bi->chanspec),
			CHSPEC_CHANNEL(bi->chanspec) <= CH_MAX_2G_CHANNEL ?
			WF_CHAN_FACTOR_2_4_G : WF_CHAN_FACTOR_5_G);
		iwe.u.freq.e = 6;
		event = IWE_STREAM_ADD_EVENT(info, event, end, &iwe, IW_EV_FREQ_LEN);

		/* Channel quality */
		iwe.cmd = IWEVQUAL;
		iwe.u.qual.qual = rssi_to_qual(dtoh16(bi->RSSI));
		iwe.u.qual.level = 0x100 + dtoh16(bi->RSSI);
		iwe.u.qual.noise = 0x100 + bi->phy_noise;
		event = IWE_STREAM_ADD_EVENT(info, event, end, &iwe, IW_EV_QUAL_LEN);

		/* WPA, WPA2, WPS, WAPI IEs */
		 wl_iw_handle_scanresults_ies(&event, end, info, bi);

		/* Encryption */
		iwe.cmd = SIOCGIWENCODE;
		if (dtoh16(bi->capability) & DOT11_CAP_PRIVACY)
			iwe.u.data.flags = IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;
		else
			iwe.u.data.flags = IW_ENCODE_DISABLED;
		iwe.u.data.length = 0;
		event = IWE_STREAM_ADD_POINT(info, event, end, &iwe, (char *)event);

		/* Rates */
		if (bi->rateset.count) {
			if (((event -extra) + IW_EV_LCP_LEN) <= (uintptr)end) {
				value = event + IW_EV_LCP_LEN;
				iwe.cmd = SIOCGIWRATE;
				/* Those two flags are ignored... */
				iwe.u.bitrate.fixed = iwe.u.bitrate.disabled = 0;
				for (j = 0; j < bi->rateset.count && j < IW_MAX_BITRATES; j++) {
					iwe.u.bitrate.value =
						(bi->rateset.rates[j] & 0x7f) * 500000;
					value = IWE_STREAM_ADD_VALUE(info, event, value, end, &iwe,
						IW_EV_PARAM_LEN);
				}
				event = value;
			}
		}
	}

	if ((ret = (event - extra)) < 0) {
		WL_ERROR(("==> Wrong size\n"));
		ret = 0;
	}

	WL_TRACE(("%s: size=%d bytes prepared \n", __FUNCTION__, (unsigned int)(event - extra)));
	return (uint)ret;
}

static int
wl_iw_get_scan(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_point *dwrq,
	char *extra
)
{
	channel_info_t ci;
	wl_scan_results_t *list_merge;
	wl_scan_results_t *list = (wl_scan_results_t *) g_scan;
	int error;
	uint buflen_from_user = dwrq->length;
	uint len =  G_SCAN_RESULTS;
	__u16 len_ret = 0;
#if  !defined(CSCAN)
	__u16 merged_len = 0;
#endif
#if defined(WL_IW_USE_ISCAN)
	iscan_info_t *iscan = g_iscan;
	iscan_buf_t * p_buf;
#if  !defined(CSCAN)
	uint32 counter = 0;
#endif /* (OEM_ANDROID) */
#endif /* (WL_IW_USE_ISCAN) */

	WL_TRACE(("%s: buflen_from_user %d: \n", dev->name, buflen_from_user));

	if (!extra) {
		WL_TRACE(("%s: wl_iw_get_scan return -EINVAL\n", dev->name));
		return -EINVAL;
	}

	/* Check for scan in progress */
	if ((error = dev_wlc_ioctl(dev, WLC_GET_CHANNEL, &ci, sizeof(ci))))
		return error;
	ci.scan_channel = dtoh32(ci.scan_channel);
	if (ci.scan_channel)
		return -EAGAIN;

#if  !defined(CSCAN)
	if (g_ss_cache_ctrl.m_timer_expired) {
		wl_iw_free_ss_cache();
		g_ss_cache_ctrl.m_timer_expired ^= 1;
	}
	if ((!g_scan_specified_ssid && g_ss_cache_ctrl.m_prev_scan_mode) ||
		g_ss_cache_ctrl.m_cons_br_scan_cnt > 4) {
		g_ss_cache_ctrl.m_cons_br_scan_cnt = 0;
		/*
		* if current scan is broadcast scan && previous scan is specific scan, and then
		* it checks the specific scan cache and removes bss from cache if it was not
		* updated in previous scan round.
		* Another possibile case is that all of specific scan gets canceled by user's
		* removing hidden AP list. In this case, if 4 consecutive times of broadcast
		* scan happens, it checks cache and remove anyone who still remains in the cache
		*/
		wl_iw_reset_ss_cache();
	}
	g_ss_cache_ctrl.m_prev_scan_mode = g_scan_specified_ssid;
	if (g_scan_specified_ssid) {
		g_ss_cache_ctrl.m_cons_br_scan_cnt = 0;
	}
	else {
		g_ss_cache_ctrl.m_cons_br_scan_cnt++;
	}
#endif 


	/* For specific SSID allocate local buffer */
	if (g_scan_specified_ssid) {
		/* Overwrite list as pointer for local buf instead of global g_scan */
		list = kmalloc(len, GFP_KERNEL);
		if (!list) {
			WL_TRACE(("%s: wl_iw_get_scan return -ENOMEM\n", dev->name));
			g_scan_specified_ssid = 0;
			return -ENOMEM;
		}
	}

	memset(list, 0, len);
	list->buflen = htod32(len);
	if ((error = dev_wlc_ioctl(dev, WLC_SCAN_RESULTS, list, len))) {
		WL_ERROR(("%s: %s : Scan_results ERROR %d\n", dev->name, __FUNCTION__, error));
		dwrq->length = len;
		if (g_scan_specified_ssid) {
			g_scan_specified_ssid = 0;
			kfree(list);
		}
		return 0;
	}
	list->buflen = dtoh32(list->buflen);
	list->version = dtoh32(list->version);
	list->count = dtoh32(list->count);

	/* check if scan results are valid and exit if not */
	if (list->version != WL_BSS_INFO_VERSION) {
		WL_ERROR(("%s : list->version %d != WL_BSS_INFO_VERSION\n",
		          __FUNCTION__, list->version));
		if (g_scan_specified_ssid) {
			g_scan_specified_ssid = 0;
			kfree(list);
		}
		return -EINVAL;
	}

#if  !defined(CSCAN)
	if (g_scan_specified_ssid) {
		/* add newly specifically scanned AP into specific scan cache */
		wl_iw_add_bss_to_ss_cache(list);
		kfree(list);
	}
#endif

#if  !defined(CSCAN)
	DHD_OS_MUTEX_LOCK(&wl_cache_lock);
#if defined(WL_IW_USE_ISCAN)
	if (g_scan_specified_ssid)
		WL_TRACE(("%s: Specified scan APs from scan=%d\n", __FUNCTION__, list->count));
	p_buf = iscan->list_hdr;
	/* Get scan results */
	while (p_buf != iscan->list_cur) {
		list_merge = &((wl_iscan_results_t*)p_buf->iscan_buf)->results;
		WL_TRACE(("%s: Bcast APs list=%d\n", __FUNCTION__, list_merge->count));
		counter += list_merge->count;
		if (list_merge->count > 0)
			len_ret += (__u16) wl_iw_get_scan_prep(list_merge, info,
			    extra+len_ret, buflen_from_user -len_ret);
		p_buf = p_buf->next;
	}
	WL_TRACE(("%s merged with total Bcast APs=%d\n", __FUNCTION__, counter));
#else
	list_merge = (wl_scan_results_t *) g_scan;
	len_ret = (__u16) wl_iw_get_scan_prep(list_merge, info, extra, buflen_from_user);
#endif /* (WL_IW_USE_ISCAN) */
	DHD_OS_MUTEX_UNLOCK(&wl_cache_lock);
	if (g_ss_cache_ctrl.m_link_down) {
		/*
		 *	delete current active bss from specific scan cache if current link is down.
		 *	this helps new scan result to be updated to Android connection manager
		 *    quickly
		*/
		wl_iw_delete_bss_from_ss_cache(g_ss_cache_ctrl.m_active_bssid);
	}
	/* merge specific scan cache into entire scan result cache */
	wl_iw_merge_scan_cache(info, extra+len_ret, buflen_from_user-len_ret, &merged_len);
	len_ret += merged_len;
	wl_iw_run_ss_cache_timer(0);
	wl_iw_run_ss_cache_timer(1);
#else	/* #if defined(OEM_ANDROID) */

	/* Merge results from specific SSID with results from Broadcast SSID */
	if (g_scan_specified_ssid) {
		WL_TRACE(("%s: Specified scan APs in the list =%d\n", __FUNCTION__, list->count));
		len_ret = (__u16) wl_iw_get_scan_prep(list, info, extra, buflen_from_user);
		kfree(list);

#if defined(WL_IW_USE_ISCAN)
		p_buf = iscan->list_hdr;
		/* Get scan results */
		while (p_buf != iscan->list_cur) {
			list_merge = &((wl_iscan_results_t*)p_buf->iscan_buf)->results;
			WL_TRACE(("%s: Bcast APs list=%d\n", __FUNCTION__, list_merge->count));
			if (list_merge->count > 0)
				len_ret += (__u16) wl_iw_get_scan_prep(list_merge, info,
				    extra+len_ret, buflen_from_user -len_ret);
			p_buf = p_buf->next;
		}
#else
		list_merge = (wl_scan_results_t *) g_scan;
		WL_TRACE(("%s: Bcast APs list=%d\n", __FUNCTION__, list_merge->count));
		if (list_merge->count > 0)
			len_ret += (__u16) wl_iw_get_scan_prep(list_merge, info, extra+len_ret,
				buflen_from_user -len_ret);
#endif /* WL_IW_USE_ISCAN */
	}
	else {
		list = (wl_scan_results_t *) g_scan;
		len_ret = (__u16) wl_iw_get_scan_prep(list, info, extra, buflen_from_user);
	}
#endif	

#if defined(WL_IW_USE_ISCAN)
	/* Clean up when specific scan results retrived */
	g_scan_specified_ssid = 0;
#endif /* WL_IW_USE_ISCAN */
	/* Force WPA_SUPPLICANT to make another scan request if
	 */
	if ((len_ret + WE_ADD_EVENT_FIX) < buflen_from_user)
		len = len_ret;

	dwrq->length = len;
	dwrq->flags = 0;	/* todo */

	WL_TRACE(("%s return to WE %d bytes APs=%d\n", __FUNCTION__, dwrq->length, list->count));
	return 0;
}

#if defined(WL_IW_USE_ISCAN)
static int
wl_iw_iscan_get_scan(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_point *dwrq,
	char *extra
)
{
	wl_scan_results_t *list;
	struct iw_event	iwe;
	wl_bss_info_t *bi = NULL;
	int ii, j;
	int apcnt;
	char *event = extra, *end = extra + dwrq->length, *value;
	iscan_info_t *iscan = g_iscan;
	iscan_buf_t * p_buf;
	uint32  counter = 0;
	uint8   channel;
#if !defined(CSCAN)
	__u16 merged_len = 0;
	uint buflen_from_user = dwrq->length;
#endif

	WL_TRACE(("%s %s buflen_from_user %d:\n", dev->name, __FUNCTION__, dwrq->length));

#if defined(SOFTAP)
	if (ap_cfg_running) {
		WL_TRACE(("%s: Not executed, reason -'SOFTAP is active'\n", __FUNCTION__));
		return -EINVAL;
	}
#endif

	if (!extra) {
		WL_TRACE(("%s: INVALID SIOCGIWSCAN GET bad parameter\n", dev->name));
		return -EINVAL;
	}

	if (g_first_broadcast_scan < BROADCAST_SCAN_FIRST_RESULT_READY) {
		WL_TRACE(("%s %s: first ISCAN results are NOT ready yet \n",
		          dev->name, __FUNCTION__));
		return -EAGAIN;
	}
	/* use backup if our thread is not successful */
	if ((!iscan) || (iscan->sysioc_pid < 0)) {
		WL_ERROR(("%ssysioc_pid\n", __FUNCTION__));
		return EAGAIN;
	}

	/* ANDROID wpa_supplicant has a bug in that it only try to get the result twice
	   and then give up, disregard of our return -EAGAIN. What's worse, it usually
	   set the scan to passive, so only make things worse
	*/

#if !defined(CSCAN)
	if (g_ss_cache_ctrl.m_timer_expired) {
		wl_iw_free_ss_cache();
		g_ss_cache_ctrl.m_timer_expired ^= 1;
	}
	if (g_scan_specified_ssid) {
		return wl_iw_get_scan(dev, info, dwrq, extra);
	}
	else {
		if (g_ss_cache_ctrl.m_link_down) {
			/*
			 * delete current active bss from specific scan cache if current
			 * link gets down. this helps new scan result to be updated to
			 * Android connection manager quickly
			 */
			wl_iw_delete_bss_from_ss_cache(g_ss_cache_ctrl.m_active_bssid);
		}
		if (g_ss_cache_ctrl.m_prev_scan_mode || g_ss_cache_ctrl.m_cons_br_scan_cnt > 4) {
			g_ss_cache_ctrl.m_cons_br_scan_cnt = 0;
			/*
			* if current scan is broadcast scan && previous scan is specific scan,
			* and then it checks the specific scan cache and removes bss from cache
			* if it was not updated in previous scan round.
			* Another possibile case is that all of specific scan gets canceled by
			* user's removing hidden AP list. In this case, if consecutive 4 times
			* of broadcast scan happens , it checks cache and remove anyone who still
			*  remains in the cache
			*/
			wl_iw_reset_ss_cache();
		}
		g_ss_cache_ctrl.m_prev_scan_mode = g_scan_specified_ssid;
		g_ss_cache_ctrl.m_cons_br_scan_cnt++;
	}
#endif /* #ifdef OEM_ANDROID */

	WL_TRACE(("%s: SIOCGIWSCAN GET broadcast results\n", dev->name));
	apcnt = 0;
	p_buf = iscan->list_hdr;
	/* Get scan results */
	while (p_buf != iscan->list_cur) {
		list = &((wl_iscan_results_t*)p_buf->iscan_buf)->results;

		counter += list->count;

		if (list->version != WL_BSS_INFO_VERSION) {
			WL_ERROR(("%s : list->version %d != WL_BSS_INFO_VERSION\n",
			          __FUNCTION__, list->version));
			return -EINVAL;
		}

		bi = NULL;
		for (ii = 0; ii < list->count && apcnt < IW_MAX_AP; apcnt++, ii++) {
			bi = (bi ?
			      (wl_bss_info_t *)((uintptr)bi + dtoh32(bi->length)) :
			      list->bss_info);
			ASSERT(((uintptr)bi + dtoh32(bi->length)) <= ((uintptr)list +
			                                              WLC_IW_ISCAN_MAXLEN));

			/* overflow check cover fields before wpa IEs */
			if (event + ETHER_ADDR_LEN + bi->SSID_len +
			    IW_EV_UINT_LEN + IW_EV_FREQ_LEN + IW_EV_QUAL_LEN >= end)
				return -E2BIG;
			/* First entry must be the BSSID */
			iwe.cmd = SIOCGIWAP;
			iwe.u.ap_addr.sa_family = ARPHRD_ETHER;
			memcpy(iwe.u.ap_addr.sa_data, &bi->BSSID, ETHER_ADDR_LEN);
			event = IWE_STREAM_ADD_EVENT(info, event, end, &iwe, IW_EV_ADDR_LEN);

			/* SSID */
			iwe.u.data.length = dtoh32(bi->SSID_len);
			iwe.cmd = SIOCGIWESSID;
			iwe.u.data.flags = 1;
			event = IWE_STREAM_ADD_POINT(info, event, end, &iwe, bi->SSID);

			/* Mode */
			if (dtoh16(bi->capability) & (DOT11_CAP_ESS | DOT11_CAP_IBSS)) {
				iwe.cmd = SIOCGIWMODE;
				if (dtoh16(bi->capability) & DOT11_CAP_ESS)
					iwe.u.mode = IW_MODE_INFRA;
				else
					iwe.u.mode = IW_MODE_ADHOC;
				event = IWE_STREAM_ADD_EVENT(info, event, end,
				                             &iwe, IW_EV_UINT_LEN);
			}

			/* Channel */
			iwe.cmd = SIOCGIWFREQ;
			channel = (bi->ctl_ch == 0) ? CHSPEC_CHANNEL(bi->chanspec) : bi->ctl_ch;
			iwe.u.freq.m = wf_channel2mhz(channel,
			                              channel <= CH_MAX_2G_CHANNEL ?
			                              WF_CHAN_FACTOR_2_4_G : WF_CHAN_FACTOR_5_G);
			iwe.u.freq.e = 6;
			event = IWE_STREAM_ADD_EVENT(info, event, end, &iwe, IW_EV_FREQ_LEN);

			/* Channel quality */
			iwe.cmd = IWEVQUAL;
			iwe.u.qual.qual = rssi_to_qual(dtoh16(bi->RSSI));
			iwe.u.qual.level = 0x100 + dtoh16(bi->RSSI);
			iwe.u.qual.noise = 0x100 + bi->phy_noise;
			event = IWE_STREAM_ADD_EVENT(info, event, end, &iwe, IW_EV_QUAL_LEN);

			/* WPA, WPA2, WPS, WAPI IEs */
			wl_iw_handle_scanresults_ies(&event, end, info, bi);

			/* Encryption */
			iwe.cmd = SIOCGIWENCODE;
			if (dtoh16(bi->capability) & DOT11_CAP_PRIVACY)
				iwe.u.data.flags = IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;
			else
				iwe.u.data.flags = IW_ENCODE_DISABLED;
			iwe.u.data.length = 0;
			event = IWE_STREAM_ADD_POINT(info, event, end, &iwe, (char *)event);

			/* Rates */
			if (bi->rateset.count) {
				if (event + IW_MAX_BITRATES*IW_EV_PARAM_LEN >= end)
					return -E2BIG;

				value = event + IW_EV_LCP_LEN;
				iwe.cmd = SIOCGIWRATE;
				/* Those two flags are ignored... */
				iwe.u.bitrate.fixed = iwe.u.bitrate.disabled = 0;
				for (j = 0; j < bi->rateset.count && j < IW_MAX_BITRATES; j++) {
					iwe.u.bitrate.value =
					        (bi->rateset.rates[j] & 0x7f) * 500000;
					value = IWE_STREAM_ADD_VALUE(info, event, value, end, &iwe,
					                             IW_EV_PARAM_LEN);
				}
				event = value;
			}
		}
		p_buf = p_buf->next;
	} /* while (p_buf) */

	dwrq->length = event - extra;
	dwrq->flags = 0;	/* todo */

#if !defined(CSCAN)
	/* merge specific scan cache into entire scan result cache */
	wl_iw_merge_scan_cache(info, event, buflen_from_user - dwrq->length, &merged_len);
	dwrq->length += merged_len;
	wl_iw_run_ss_cache_timer(0);
	wl_iw_run_ss_cache_timer(1);
#endif /* CSCAN */
	/* first brodcast scan results gets consumed */
	g_first_broadcast_scan = BROADCAST_SCAN_FIRST_RESULT_CONSUMED;

	WL_TRACE(("%s return to WE %d bytes APs=%d\n", __FUNCTION__, dwrq->length, counter));

	/* if get scan was called too fast (aka iwlist) results maybe empty hence return again */
	if (!dwrq->length)
		return -EAGAIN;

	return 0;
}
#endif /* WIRELESS_EXT > 13 */

#define WL_JOIN_PARAMS_MAX 1600
#ifdef CONFIG_PRESCANNED
static int
check_prescan(wl_join_params_t *join_params, int *join_params_size)
{
	int cnt = 0;
	int indx = 0;
	wl_iw_ss_cache_t *node = NULL;
	wl_bss_info_t *bi = NULL;
	iscan_info_t *iscan = g_iscan;
	iscan_buf_t * buf;
	wl_scan_results_t *list;
	char *destbuf;

	buf = iscan->list_hdr;

	while (buf) {
		list = &((wl_iscan_results_t*)buf->iscan_buf)->results;
		bi = NULL;
		for (indx = 0;  indx < list->count; indx++) {
			bi = bi ? (wl_bss_info_t *)((uintptr)bi + dtoh32(bi->length))
				: list->bss_info;
			if (!(dtoh16(bi->capability) & DOT11_CAP_ESS))
				continue;
			if ((dtoh32(bi->SSID_len) != join_params->ssid.SSID_len) ||
				memcmp(bi->SSID, join_params->ssid.SSID,
				join_params->ssid.SSID_len))
				continue;
			memcpy(&join_params->params.chanspec_list[cnt],
				&bi->chanspec, sizeof(chanspec_t));
			WL_ERROR(("iscan : chanspec :%d, count %d \n", bi->chanspec, cnt));
			cnt++;
		}
		buf = buf->next;
	}

	if (!cnt) {
		MUTEX_LOCK_WL_SCAN_SET();
		node = g_ss_cache_ctrl.m_cache_head;
		for (; node; ) {
			if (!memcmp(&node->bss_info->SSID, join_params->ssid.SSID,
				join_params->ssid.SSID_len)) {
				memcpy(&join_params->params.chanspec_list[cnt],
					&node->bss_info->chanspec, sizeof(chanspec_t));
				WL_ERROR(("cache_scan : chanspec :%d, count %d \n",
				(int)node->bss_info->chanspec, cnt));
				cnt++;
			}
			node = node->next;
		}
		MUTEX_UNLOCK_WL_SCAN_SET();
	}

	if (!cnt)
		return 0;

	destbuf = (char *)&join_params->params.chanspec_list[cnt];
	*join_params_size = destbuf - (char*)join_params;
	join_params->ssid.SSID_len = htod32(g_ssid.SSID_len);
	memcpy(&(join_params->params.bssid), &ether_bcast, ETHER_ADDR_LEN);
	join_params->params.chanspec_num = htod32(cnt);

	if ((*join_params_size) > WL_JOIN_PARAMS_MAX) {
		WL_ERROR(("can't fit bssids for all %d APs found\n", cnt));
			kfree(join_params);
		return 0;
	}

	WL_ERROR(("Passing %d channel/bssid pairs.\n", cnt));
	return cnt;
}
#endif /* CONFIG_PRESCANNED */

static int
wl_iw_set_essid(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_point *dwrq,
	char *extra
)
{
	int error;
	wl_join_params_t *join_params;
	int join_params_size;

	WL_TRACE(("%s: SIOCSIWESSID\n", dev->name));

	RETURN_IF_EXTRA_NULL(extra);

#ifdef OEM_CHROMIUMOS
	if (g_set_essid_before_scan)
		return -EAGAIN;
#endif
	if (!(join_params = kmalloc(WL_JOIN_PARAMS_MAX, GFP_KERNEL))) {
		WL_ERROR(("allocation failed for join_params size is %d\n", WL_JOIN_PARAMS_MAX));
		return -ENOMEM;
	}

	memset(join_params, 0, WL_JOIN_PARAMS_MAX);

	/* clean cached ssid value */
	memset(&g_ssid, 0, sizeof(g_ssid));

	if (dwrq->length && extra) {
#if WIRELESS_EXT > 20
		g_ssid.SSID_len = MIN(sizeof(g_ssid.SSID), dwrq->length);
#else
		g_ssid.SSID_len = MIN(sizeof(g_ssid.SSID), dwrq->length-1);
#endif
		memcpy(g_ssid.SSID, extra, g_ssid.SSID_len);

#ifdef CONFIG_PRESCANNED
		memcpy(join_params->ssid.SSID, g_ssid.SSID, g_ssid.SSID_len);
		join_params->ssid.SSID_len = g_ssid.SSID_len;

		if (check_prescan(join_params, &join_params_size)) {
			if ((error = dev_wlc_ioctl(dev, WLC_SET_SSID,
				join_params, join_params_size))) {
				WL_ERROR(("Invalid ioctl data=%d\n", error));
				kfree(join_params);
				return error;
			}
			kfree(join_params);
			return 0;
		} else {
			WL_ERROR(("No matched found\n Trying to join to specific channel\n"));
		}
#endif /* CONFIG_PRESCANNED */
	} else {
		/* Broadcast SSID */
		g_ssid.SSID_len = 0;
	}
	g_ssid.SSID_len = htod32(g_ssid.SSID_len);

	/* join parameters starts with the ssid */
	memset(join_params, 0, sizeof(join_params));
	join_params_size = sizeof(join_params->ssid);

	memcpy(join_params->ssid.SSID, g_ssid.SSID, g_ssid.SSID_len);
	join_params->ssid.SSID_len = htod32(g_ssid.SSID_len);
	memcpy(&(join_params->params.bssid), &ether_bcast, ETHER_ADDR_LEN);

	/* join  to specific channel , use 0 for all  channels */
	/* g_wl_iw_params.target_channel = 0; */
	wl_iw_ch_to_chanspec(g_wl_iw_params.target_channel, join_params, &join_params_size);

	if ((error = dev_wlc_ioctl(dev, WLC_SET_SSID, join_params, join_params_size))) {
		WL_ERROR(("Invalid ioctl data=%d\n", error));
		return error;
	}

	if (g_ssid.SSID_len) {
		WL_ERROR(("%s: join SSID=%s ch=%d\n", __FUNCTION__,
			g_ssid.SSID,  g_wl_iw_params.target_channel));
	}
	kfree(join_params);
	return 0;
}

static int
wl_iw_get_essid(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_point *dwrq,
	char *extra
)
{
	wlc_ssid_t ssid;
	int error;

	WL_TRACE(("%s: SIOCGIWESSID\n", dev->name));

	if (!extra)
		return -EINVAL;

	if ((error = dev_wlc_ioctl(dev, WLC_GET_SSID, &ssid, sizeof(ssid)))) {
		WL_ERROR(("Error getting the SSID\n"));
		return error;
	}

	ssid.SSID_len = dtoh32(ssid.SSID_len);

	/* Get the current SSID */
	memcpy(extra, ssid.SSID, ssid.SSID_len);

	dwrq->length = ssid.SSID_len;

	dwrq->flags = 1; /* active */

	return 0;
}

static int
wl_iw_set_nick(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_point *dwrq,
	char *extra
)
{
	wl_iw_t *iw = NETDEV_PRIV(dev);

	WL_TRACE(("%s: SIOCSIWNICKN\n", dev->name));

	if (!extra)
		return -EINVAL;

	/* Check the size of the string */
	if (dwrq->length > sizeof(iw->nickname))
		return -E2BIG;

	memcpy(iw->nickname, extra, dwrq->length);
	iw->nickname[dwrq->length - 1] = '\0';

	return 0;
}

static int
wl_iw_get_nick(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_point *dwrq,
	char *extra
)
{
	wl_iw_t *iw = NETDEV_PRIV(dev);

	WL_TRACE(("%s: SIOCGIWNICKN\n", dev->name));

	if (!extra)
		return -EINVAL;

	strcpy(extra, iw->nickname);
	dwrq->length = strlen(extra) + 1;

	return 0;
}

static int
wl_iw_set_rate(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_param *vwrq,
	char *extra
)
{
	wl_rateset_t rateset;
	int error, rate, i, error_bg, error_a;

	WL_TRACE(("%s: SIOCSIWRATE\n", dev->name));

	/* Get current rateset */
	if ((error = dev_wlc_ioctl(dev, WLC_GET_CURR_RATESET, &rateset, sizeof(rateset))))
		return error;

	rateset.count = dtoh32(rateset.count);

	if (vwrq->value < 0) {
		/* Select maximum rate */
		rate = rateset.rates[rateset.count - 1] & 0x7f;
	} else if (vwrq->value < rateset.count) {
		/* Select rate by rateset index */
		rate = rateset.rates[vwrq->value] & 0x7f;
	} else {
		/* Specified rate in bps */
		rate = vwrq->value / 500000;
	}

	if (vwrq->fixed) {
		/*
			Set rate override,
			Since the is a/b/g-blind, both a/bg_rate are enforced.
		*/
		error_bg = dev_wlc_intvar_set(dev, "bg_rate", rate);
		error_a = dev_wlc_intvar_set(dev, "a_rate", rate);

		if (error_bg && error_a)
			return (error_bg | error_a);
	} else {
		/*
			clear rate override
			Since the is a/b/g-blind, both a/bg_rate are enforced.
		*/
		/* 0 is for clearing rate override */
		error_bg = dev_wlc_intvar_set(dev, "bg_rate", 0);
		/* 0 is for clearing rate override */
		error_a = dev_wlc_intvar_set(dev, "a_rate", 0);

		if (error_bg && error_a)
			return (error_bg | error_a);

		/* Remove rates above selected rate */
		for (i = 0; i < rateset.count; i++)
			if ((rateset.rates[i] & 0x7f) > rate)
				break;
		rateset.count = htod32(i);

		/* Set current rateset */
		if ((error = dev_wlc_ioctl(dev, WLC_SET_RATESET, &rateset, sizeof(rateset))))
			return error;
	}

	return 0;
}

static int
wl_iw_get_rate(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_param *vwrq,
	char *extra
)
{
	int error, rate;

	WL_TRACE(("%s: SIOCGIWRATE\n", dev->name));

	/* Report the current tx rate */
	if ((error = dev_wlc_ioctl(dev, WLC_GET_RATE, &rate, sizeof(rate))))
		return error;
	rate = dtoh32(rate);
	vwrq->value = rate * 500000;

	return 0;
}

static int
wl_iw_set_rts(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_param *vwrq,
	char *extra
)
{
	int error, rts;

	WL_TRACE(("%s: SIOCSIWRTS\n", dev->name));

	if (vwrq->disabled)
		rts = DOT11_DEFAULT_RTS_LEN;
	else if (vwrq->value < 0 || vwrq->value > DOT11_DEFAULT_RTS_LEN)
		return -EINVAL;
	else
		rts = vwrq->value;

	if ((error = dev_wlc_intvar_set(dev, "rtsthresh", rts)))
		return error;

	return 0;
}

static int
wl_iw_get_rts(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_param *vwrq,
	char *extra
)
{
	int error, rts;

	WL_TRACE(("%s: SIOCGIWRTS\n", dev->name));

	if ((error = dev_wlc_intvar_get(dev, "rtsthresh", &rts)))
		return error;

	vwrq->value = rts;
	vwrq->disabled = (rts >= DOT11_DEFAULT_RTS_LEN);
	vwrq->fixed = 1;

	return 0;
}

static int
wl_iw_set_frag(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_param *vwrq,
	char *extra
)
{
	int error, frag;

	WL_TRACE(("%s: SIOCSIWFRAG\n", dev->name));

	if (vwrq->disabled)
		frag = DOT11_DEFAULT_FRAG_LEN;
	else if (vwrq->value < 0 || vwrq->value > DOT11_DEFAULT_FRAG_LEN)
		return -EINVAL;
	else
		frag = vwrq->value;

	if ((error = dev_wlc_intvar_set(dev, "fragthresh", frag)))
		return error;

	return 0;
}

static int
wl_iw_get_frag(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_param *vwrq,
	char *extra
)
{
	int error, fragthreshold;

	WL_TRACE(("%s: SIOCGIWFRAG\n", dev->name));

	if ((error = dev_wlc_intvar_get(dev, "fragthresh", &fragthreshold)))
		return error;

	vwrq->value = fragthreshold;
	vwrq->disabled = (fragthreshold >= DOT11_DEFAULT_FRAG_LEN);
	vwrq->fixed = 1;

	return 0;
}

static int
wl_iw_set_txpow(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_param *vwrq,
	char *extra
)
{
	int error, disable;
	uint16 txpwrmw;
	WL_TRACE(("%s: SIOCSIWTXPOW\n", dev->name));

	/* Make sure radio is off or on as far as software is concerned */
	disable = vwrq->disabled ? WL_RADIO_SW_DISABLE : 0;
	disable += WL_RADIO_SW_DISABLE << 16;

	disable = htod32(disable);
	if ((error = dev_wlc_ioctl(dev, WLC_SET_RADIO, &disable, sizeof(disable))))
		return error;

	/* If Radio is off, nothing more to do */
	if (disable & WL_RADIO_SW_DISABLE)
		return 0;

	/* Only handle mW */
	if (!(vwrq->flags & IW_TXPOW_MWATT))
		return -EINVAL;

	/* Value < 0 means just "on" or "off" */
	if (vwrq->value < 0)
		return 0;

	if (vwrq->value > 0xffff) txpwrmw = 0xffff;
	else txpwrmw = (uint16)vwrq->value;


	error = dev_wlc_intvar_set(dev, "qtxpower", (int)(bcm_mw_to_qdbm(txpwrmw)));
	return error;
}

static int
wl_iw_get_txpow(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_param *vwrq,
	char *extra
)
{
	int error, disable, txpwrdbm;
	uint8 result;

	WL_TRACE(("%s: SIOCGIWTXPOW\n", dev->name));

	if ((error = dev_wlc_ioctl(dev, WLC_GET_RADIO, &disable, sizeof(disable))) ||
	    (error = dev_wlc_intvar_get(dev, "qtxpower", &txpwrdbm)))
		return error;

	disable = dtoh32(disable);
	result = (uint8)(txpwrdbm & ~WL_TXPWR_OVERRIDE);
	vwrq->value = (int32)bcm_qdbm_to_mw(result);
	vwrq->fixed = 0;
	vwrq->disabled = (disable & (WL_RADIO_SW_DISABLE | WL_RADIO_HW_DISABLE)) ? 1 : 0;
	vwrq->flags = IW_TXPOW_MWATT;

	return 0;
}

#if WIRELESS_EXT > 10
static int
wl_iw_set_retry(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_param *vwrq,
	char *extra
)
{
	int error, lrl, srl;

	WL_TRACE(("%s: SIOCSIWRETRY\n", dev->name));

	/* Do not handle "off" or "lifetime" */
	if (vwrq->disabled || (vwrq->flags & IW_RETRY_LIFETIME))
		return -EINVAL;

	/* Handle "[min|max] limit" */
	if (vwrq->flags & IW_RETRY_LIMIT) {

		/* "max limit" or just "limit" */
#if WIRELESS_EXT > 20
	if ((vwrq->flags & IW_RETRY_LONG) ||(vwrq->flags & IW_RETRY_MAX) ||
		!((vwrq->flags & IW_RETRY_SHORT) || (vwrq->flags & IW_RETRY_MIN))) {
#else
	if ((vwrq->flags & IW_RETRY_MAX) || !(vwrq->flags & IW_RETRY_MIN)) {
#endif /* WIRELESS_EXT > 20 */
			lrl = htod32(vwrq->value);
			if ((error = dev_wlc_ioctl(dev, WLC_SET_LRL, &lrl, sizeof(lrl))))
				return error;
		}

		/* "min limit" or just "limit" */
#if WIRELESS_EXT > 20
	if ((vwrq->flags & IW_RETRY_SHORT) ||(vwrq->flags & IW_RETRY_MIN) ||
		!((vwrq->flags & IW_RETRY_LONG) || (vwrq->flags & IW_RETRY_MAX))) {
#else
		if ((vwrq->flags & IW_RETRY_MIN) || !(vwrq->flags & IW_RETRY_MAX)) {
#endif /* WIRELESS_EXT > 20 */
			srl = htod32(vwrq->value);
			if ((error = dev_wlc_ioctl(dev, WLC_SET_SRL, &srl, sizeof(srl))))
				return error;
		}
	}
	return 0;
}

static int
wl_iw_get_retry(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_param *vwrq,
	char *extra
)
{
	int error, lrl, srl;

	WL_TRACE(("%s: SIOCGIWRETRY\n", dev->name));

	vwrq->disabled = 0;      /* Can't be disabled */

	/* Do not handle lifetime queries */
	if ((vwrq->flags & IW_RETRY_TYPE) == IW_RETRY_LIFETIME)
		return -EINVAL;

	/* Get retry limits */
	if ((error = dev_wlc_ioctl(dev, WLC_GET_LRL, &lrl, sizeof(lrl))) ||
	    (error = dev_wlc_ioctl(dev, WLC_GET_SRL, &srl, sizeof(srl))))
		return error;

	lrl = dtoh32(lrl);
	srl = dtoh32(srl);

	/* Note : by default, display the min retry number */
	if (vwrq->flags & IW_RETRY_MAX) {
		vwrq->flags = IW_RETRY_LIMIT | IW_RETRY_MAX;
		vwrq->value = lrl;
	} else {
		vwrq->flags = IW_RETRY_LIMIT;
		vwrq->value = srl;
		if (srl != lrl)
			vwrq->flags |= IW_RETRY_MIN;
	}

	return 0;
}
#endif /* WIRELESS_EXT > 10 */

static int
wl_iw_set_encode(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_point *dwrq,
	char *extra
)
{
	wl_wsec_key_t key;
	int error, val, wsec;

	WL_TRACE(("%s: SIOCSIWENCODE index %d, len %d, flags %04x (%s%s%s%s%s)\n",
		dev->name, dwrq->flags & IW_ENCODE_INDEX, dwrq->length, dwrq->flags,
		dwrq->flags & IW_ENCODE_NOKEY ? "NOKEY" : "",
		dwrq->flags & IW_ENCODE_DISABLED ? " DISABLED" : "",
		dwrq->flags & IW_ENCODE_RESTRICTED ? " RESTRICTED" : "",
		dwrq->flags & IW_ENCODE_OPEN ? " OPEN" : "",
		dwrq->flags & IW_ENCODE_TEMP ? " TEMP" : ""));

	memset(&key, 0, sizeof(key));

	if ((dwrq->flags & IW_ENCODE_INDEX) == 0) {
		/* Find the current key */
		for (key.index = 0; key.index < DOT11_MAX_DEFAULT_KEYS; key.index++) {
			val = htod32(key.index);
			if ((error = dev_wlc_ioctl(dev, WLC_GET_KEY_PRIMARY, &val, sizeof(val))))
				return error;
			val = dtoh32(val);
			if (val)
				break;
		}
		/* Default to 0 */
		if (key.index == DOT11_MAX_DEFAULT_KEYS)
			key.index = 0;
	} else {
		key.index = (dwrq->flags & IW_ENCODE_INDEX) - 1;
		if (key.index >= DOT11_MAX_DEFAULT_KEYS)
			return -EINVAL;
	}

	/* Old API used to pass a NULL pointer instead of IW_ENCODE_NOKEY */
	if (!extra || !dwrq->length || (dwrq->flags & IW_ENCODE_NOKEY)) {
		/* Just select a new current key */
		val = htod32(key.index);
		if ((error = dev_wlc_ioctl(dev, WLC_SET_KEY_PRIMARY, &val, sizeof(val))))
			return error;
	} else {
		key.len = dwrq->length;

		if (dwrq->length > sizeof(key.data))
			return -EINVAL;

		memcpy(key.data, extra, dwrq->length);

		key.flags = WL_PRIMARY_KEY;
		switch (key.len) {
		case WEP1_KEY_SIZE:
			key.algo = CRYPTO_ALGO_WEP1;
			break;
		case WEP128_KEY_SIZE:
			key.algo = CRYPTO_ALGO_WEP128;
			break;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 14)
		case TKIP_KEY_SIZE:
			key.algo = CRYPTO_ALGO_TKIP;
			break;
#endif
		case AES_KEY_SIZE:
			key.algo = CRYPTO_ALGO_AES_CCM;
			break;
		default:
			return -EINVAL;
		}

		/* Set the new key/index */
		swap_key_from_BE(&key);
		if ((error = dev_wlc_ioctl(dev, WLC_SET_KEY, &key, sizeof(key))))
			return error;
	}

	/* Interpret "off" to mean no encryption */
	val = (dwrq->flags & IW_ENCODE_DISABLED) ? 0 : WEP_ENABLED;

	if ((error = dev_wlc_intvar_get(dev, "wsec", &wsec)))
		return error;

	wsec  &= ~(WEP_ENABLED);
	wsec |= val;

	if ((error = dev_wlc_intvar_set(dev, "wsec", wsec)))
		return error;

	/* Interpret "restricted" to mean shared key authentication */
	val = (dwrq->flags & IW_ENCODE_RESTRICTED) ? 1 : 0;
	val = htod32(val);
	if ((error = dev_wlc_ioctl(dev, WLC_SET_AUTH, &val, sizeof(val))))
		return error;

	return 0;
}

static int
wl_iw_get_encode(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_point *dwrq,
	char *extra
)
{
	wl_wsec_key_t key;
	int error, val, wsec, auth;

	WL_TRACE(("%s: SIOCGIWENCODE\n", dev->name));

	/* assure default values of zero for things we don't touch */
	bzero(&key, sizeof(wl_wsec_key_t));

	if ((dwrq->flags & IW_ENCODE_INDEX) == 0) {
		/* Find the current key */
		for (key.index = 0; key.index < DOT11_MAX_DEFAULT_KEYS; key.index++) {
			val = key.index;
			if ((error = dev_wlc_ioctl(dev, WLC_GET_KEY_PRIMARY, &val, sizeof(val))))
				return error;
			val = dtoh32(val);
			if (val)
				break;
		}
	} else
		key.index = (dwrq->flags & IW_ENCODE_INDEX) - 1;

	if (key.index >= DOT11_MAX_DEFAULT_KEYS)
		key.index = 0;

	/* Get info */

	if ((error = dev_wlc_ioctl(dev, WLC_GET_WSEC, &wsec, sizeof(wsec))) ||
	    (error = dev_wlc_ioctl(dev, WLC_GET_AUTH, &auth, sizeof(auth))))
		return error;

	swap_key_to_BE(&key);

	wsec = dtoh32(wsec);
	auth = dtoh32(auth);
	/* Get key length */
	dwrq->length = MIN(DOT11_MAX_KEY_SIZE, key.len);

	/* Get flags */
	dwrq->flags = key.index + 1;
	if (!(wsec & (WEP_ENABLED | TKIP_ENABLED | AES_ENABLED))) {
		/* Interpret "off" to mean no encryption */
		dwrq->flags |= IW_ENCODE_DISABLED;
	}
	if (auth) {
		/* Interpret "restricted" to mean shared key authentication */
		dwrq->flags |= IW_ENCODE_RESTRICTED;
	}

	/* Get key */
	if (dwrq->length && extra)
		memcpy(extra, key.data, dwrq->length);

	return 0;
}

static int
wl_iw_set_power(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_param *vwrq,
	char *extra
)
{
	int error, pm;

	WL_TRACE(("%s: SIOCSIWPOWER\n", dev->name));

	pm = vwrq->disabled ? PM_OFF : PM_MAX;

	pm = htod32(pm);
	if ((error = dev_wlc_ioctl(dev, WLC_SET_PM, &pm, sizeof(pm))))
		return error;

	return 0;
}

static int
wl_iw_get_power(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_param *vwrq,
	char *extra
)
{
	int error, pm;

	WL_TRACE(("%s: SIOCGIWPOWER\n", dev->name));

	if ((error = dev_wlc_ioctl(dev, WLC_GET_PM, &pm, sizeof(pm))))
		return error;

	pm = dtoh32(pm);
	vwrq->disabled = pm ? 0 : 1;
	vwrq->flags = IW_POWER_ALL_R;

	return 0;
}

#if WIRELESS_EXT > 17
static int
wl_iw_set_wpaie(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_point *iwp,
	char *extra
)
{

	WL_TRACE(("%s: SIOCSIWGENIE\n", dev->name));

	RETURN_IF_EXTRA_NULL(extra);

#ifdef DHD_DEBUG
	{
		int i;

		for (i = 0; i < iwp->length; i++)
			WL_TRACE(("%02X ", extra[i]));
		WL_TRACE(("\n"));
	}
#endif

		dev_wlc_bufvar_set(dev, "wpaie", extra, iwp->length);

	return 0;
}

static int
wl_iw_get_wpaie(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_point *iwp,
	char *extra
)
{
	WL_TRACE(("%s: SIOCGIWGENIE\n", dev->name));
	iwp->length = 64;
	dev_wlc_bufvar_get(dev, "wpaie", extra, iwp->length);
	return 0;
}

static int
wl_iw_set_encodeext(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_point *dwrq,
	char *extra
)
{
	wl_wsec_key_t key;
	int error;
	struct iw_encode_ext *iwe;

	WL_TRACE(("%s: SIOCSIWENCODEEXT\n", dev->name));

	RETURN_IF_EXTRA_NULL(extra);

	memset(&key, 0, sizeof(key));
	iwe = (struct iw_encode_ext *)extra;

	/* disable encryption completely  */
	if (dwrq->flags & IW_ENCODE_DISABLED) {

	}

	/* get the key index */
	key.index = 0;
	if (dwrq->flags & IW_ENCODE_INDEX)
		key.index = (dwrq->flags & IW_ENCODE_INDEX) - 1;

	key.len = iwe->key_len;

	/* Instead of bcast for ea address for default wep keys, driver needs it to be Null */
	if (!ETHER_ISMULTI(iwe->addr.sa_data))
		bcopy((void *)&iwe->addr.sa_data, (char *)&key.ea, ETHER_ADDR_LEN);

	/* check for key index change */
	if (key.len == 0) {
		if (iwe->ext_flags & IW_ENCODE_EXT_SET_TX_KEY) {
			WL_WSEC(("Changing the the primary Key to %d\n", key.index));
			/* change the key index .... */
			key.index = htod32(key.index);
			error = dev_wlc_ioctl(dev, WLC_SET_KEY_PRIMARY,
				&key.index, sizeof(key.index));
			if (error)
				return error;
		}
		/* key delete */
		else {
			swap_key_from_BE(&key);
			dev_wlc_ioctl(dev, WLC_SET_KEY, &key, sizeof(key));
		}
	}
	else {
		if (iwe->key_len > sizeof(key.data))
			return -EINVAL;

		WL_WSEC(("Setting the key index %d\n", key.index));
		if (iwe->ext_flags & IW_ENCODE_EXT_SET_TX_KEY) {
			WL_WSEC(("key is a Primary Key\n"));
			key.flags = WL_PRIMARY_KEY;
		}

		bcopy((void *)iwe->key, key.data, iwe->key_len);

		if (iwe->alg == IW_ENCODE_ALG_TKIP) {
			uint8 keybuf[8];
			bcopy(&key.data[24], keybuf, sizeof(keybuf));
			bcopy(&key.data[16], &key.data[24], sizeof(keybuf));
			bcopy(keybuf, &key.data[16], sizeof(keybuf));
		}

		/* rx iv */
		if (iwe->ext_flags & IW_ENCODE_EXT_RX_SEQ_VALID) {
			uchar *ivptr;
			ivptr = (uchar *)iwe->rx_seq;
			key.rxiv.hi = (ivptr[5] << 24) | (ivptr[4] << 16) |
				(ivptr[3] << 8) | ivptr[2];
			key.rxiv.lo = (ivptr[1] << 8) | ivptr[0];
			key.iv_initialized = TRUE;
		}

		switch (iwe->alg) {
			case IW_ENCODE_ALG_NONE:
				key.algo = CRYPTO_ALGO_OFF;
				break;
			case IW_ENCODE_ALG_WEP:
				if (iwe->key_len == WEP1_KEY_SIZE)
					key.algo = CRYPTO_ALGO_WEP1;
				else
					key.algo = CRYPTO_ALGO_WEP128;
				break;
			case IW_ENCODE_ALG_TKIP:
				key.algo = CRYPTO_ALGO_TKIP;
				break;
			case IW_ENCODE_ALG_CCMP:
				key.algo = CRYPTO_ALGO_AES_CCM;
				break;
			default:
				break;
		}
		swap_key_from_BE(&key);

		dhd_wait_pend8021x(dev);

		error = dev_wlc_ioctl(dev, WLC_SET_KEY, &key, sizeof(key));
		if (error)
			return error;
	}
	return 0;
}

#if WIRELESS_EXT > 17
struct {
	pmkid_list_t pmkids;
	pmkid_t foo[MAXPMKID-1];
} pmkid_list;

static int
wl_iw_set_pmksa(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_param *vwrq,
	char *extra
)
{
	struct iw_pmksa *iwpmksa;
	uint i;
	int ret = 0;
	char eabuf[ETHER_ADDR_STR_LEN];

	WL_WSEC(("%s: SIOCSIWPMKSA\n", dev->name));

	RETURN_IF_EXTRA_NULL(extra);

	iwpmksa = (struct iw_pmksa *)extra;
	bzero((char *)eabuf, ETHER_ADDR_STR_LEN);

	if (iwpmksa->cmd == IW_PMKSA_FLUSH) {
		WL_WSEC(("wl_iw_set_pmksa - IW_PMKSA_FLUSH\n"));
		bzero((char *)&pmkid_list, sizeof(pmkid_list));
	}

	else if (iwpmksa->cmd == IW_PMKSA_REMOVE) {
		{
			pmkid_list_t pmkid, *pmkidptr;
			uint j;
			pmkidptr = &pmkid;

			bcopy(&iwpmksa->bssid.sa_data[0], &pmkidptr->pmkid[0].BSSID,
				ETHER_ADDR_LEN);
			bcopy(&iwpmksa->pmkid[0], &pmkidptr->pmkid[0].PMKID, WPA2_PMKID_LEN);

			WL_WSEC(("wl_iw_set_pmksa,IW_PMKSA_REMOVE - PMKID: %s = ",
				bcm_ether_ntoa(&pmkidptr->pmkid[0].BSSID,
				eabuf)));
			for (j = 0; j < WPA2_PMKID_LEN; j++)
				WL_WSEC(("%02x ", pmkidptr->pmkid[0].PMKID[j]));
			WL_WSEC(("\n"));
		}

		for (i = 0; i < pmkid_list.pmkids.npmkid; i++)
			if (!bcmp(&iwpmksa->bssid.sa_data[0], &pmkid_list.pmkids.pmkid[i].BSSID,
				ETHER_ADDR_LEN))
				break;

		if ((pmkid_list.pmkids.npmkid > 0) && (i < pmkid_list.pmkids.npmkid)) {
			bzero(&pmkid_list.pmkids.pmkid[i], sizeof(pmkid_t));
			for (; i < (pmkid_list.pmkids.npmkid - 1); i++) {
				bcopy(&pmkid_list.pmkids.pmkid[i+1].BSSID,
					&pmkid_list.pmkids.pmkid[i].BSSID,
					ETHER_ADDR_LEN);
				bcopy(&pmkid_list.pmkids.pmkid[i+1].PMKID,
					&pmkid_list.pmkids.pmkid[i].PMKID,
					WPA2_PMKID_LEN);
			}
			pmkid_list.pmkids.npmkid--;
		}
		else
			ret = -EINVAL;
	}

	else if (iwpmksa->cmd == IW_PMKSA_ADD) {
		for (i = 0; i < pmkid_list.pmkids.npmkid; i++)
			if (!bcmp(&iwpmksa->bssid.sa_data[0], &pmkid_list.pmkids.pmkid[i].BSSID,
				ETHER_ADDR_LEN))
				break;
		if (i < MAXPMKID) {
			bcopy(&iwpmksa->bssid.sa_data[0],
				&pmkid_list.pmkids.pmkid[i].BSSID,
				ETHER_ADDR_LEN);
			bcopy(&iwpmksa->pmkid[0], &pmkid_list.pmkids.pmkid[i].PMKID,
				WPA2_PMKID_LEN);
			if (i == pmkid_list.pmkids.npmkid)
				pmkid_list.pmkids.npmkid++;
		}
		else
			ret = -EINVAL;

		{
			uint j;
			uint k;
			k = pmkid_list.pmkids.npmkid;
			WL_WSEC(("wl_iw_set_pmksa,IW_PMKSA_ADD - PMKID: %s = ",
				bcm_ether_ntoa(&pmkid_list.pmkids.pmkid[k].BSSID,
				eabuf)));
			for (j = 0; j < WPA2_PMKID_LEN; j++)
				WL_WSEC(("%02x ", pmkid_list.pmkids.pmkid[k].PMKID[j]));
			WL_WSEC(("\n"));
		}
	}
	WL_WSEC(("PRINTING pmkid LIST - No of elements %d", pmkid_list.pmkids.npmkid));
	for (i = 0; i < pmkid_list.pmkids.npmkid; i++) {
		uint j;
		WL_WSEC(("\nPMKID[%d]: %s = ", i,
			bcm_ether_ntoa(&pmkid_list.pmkids.pmkid[i].BSSID,
			eabuf)));
		for (j = 0; j < WPA2_PMKID_LEN; j++)
			WL_WSEC(("%02x ", pmkid_list.pmkids.pmkid[i].PMKID[j]));
	}
	WL_WSEC(("\n"));

	if (!ret)
		ret = dev_wlc_bufvar_set(dev, "pmkid_info", (char *)&pmkid_list,
			sizeof(pmkid_list));
	return ret;
}
#endif /* WIRELESS_EXT > 17 */

static int
wl_iw_get_encodeext(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_param *vwrq,
	char *extra
)
{
	WL_TRACE(("%s: SIOCGIWENCODEEXT\n", dev->name));
	return 0;
}

/* Create wsec for wpaauth from saved privacy invoked, pairwise and group ciphers */
static uint32
wl_iw_create_wpaauth_wsec(struct net_device *dev)
{
	wl_iw_t *iw = NETDEV_PRIV(dev);
	uint32 wsec;

	/* Create initial wsec from pairwise cipher */
	if (iw->pcipher & (IW_AUTH_CIPHER_WEP40 | IW_AUTH_CIPHER_WEP104))
		wsec = WEP_ENABLED;
	else if (iw->pcipher & IW_AUTH_CIPHER_TKIP)
		wsec = TKIP_ENABLED;
	else if (iw->pcipher & IW_AUTH_CIPHER_CCMP)
		wsec = AES_ENABLED;
	else
		wsec = 0;

	/* Add into wsec the group cipher */
	if (iw->gcipher & (IW_AUTH_CIPHER_WEP40 | IW_AUTH_CIPHER_WEP104))
		wsec |= WEP_ENABLED;
	else if (iw->gcipher & IW_AUTH_CIPHER_TKIP)
		wsec |= TKIP_ENABLED;
	else if (iw->gcipher & IW_AUTH_CIPHER_CCMP)
		wsec |= AES_ENABLED;

	/* If the pairwise and group cipher did not result in a (non zero) wsec
	   value then if privacy invoked has been set then we set WSEC to
	   WEP.  This is the wlc API to create an association with the
	   privacy capability bit set.
	 */
	if (wsec == 0 && iw->privacy_invoked)
		wsec = WEP_ENABLED;

	WL_INFORM(("%s: returning wsec of %d\n", __FUNCTION__, wsec));

	return wsec;
}

static int
wl_iw_set_wpaauth(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_param *vwrq,
	char *extra
)
{
	int error = 0;
	int paramid;
	int paramval;
	int val = 0;
	wl_iw_t *iw = NETDEV_PRIV(dev);

	paramid = vwrq->flags & IW_AUTH_INDEX;
	paramval = vwrq->value;

	WL_TRACE(("%s: SIOCSIWAUTH, %s(%d), paramval = 0x%0x\n",
		dev->name,
		paramid == IW_AUTH_WPA_VERSION ? "IW_AUTH_WPA_VERSION" :
		paramid == IW_AUTH_CIPHER_PAIRWISE ? "IW_AUTH_CIPHER_PAIRWISE" :
		paramid == IW_AUTH_CIPHER_GROUP ? "IW_AUTH_CIPHER_GROUP" :
		paramid == IW_AUTH_KEY_MGMT ? "IW_AUTH_KEY_MGMT" :
		paramid == IW_AUTH_TKIP_COUNTERMEASURES ? "IW_AUTH_TKIP_COUNTERMEASURES" :
		paramid == IW_AUTH_DROP_UNENCRYPTED ? "IW_AUTH_DROP_UNENCRYPTED" :
		paramid == IW_AUTH_80211_AUTH_ALG ? "IW_AUTH_80211_AUTH_ALG" :
		paramid == IW_AUTH_WPA_ENABLED ? "IW_AUTH_WPA_ENABLED" :
		paramid == IW_AUTH_RX_UNENCRYPTED_EAPOL ? "IW_AUTH_RX_UNENCRYPTED_EAPOL" :
		paramid == IW_AUTH_ROAMING_CONTROL ? "IW_AUTH_ROAMING_CONTROL" :
		paramid == IW_AUTH_PRIVACY_INVOKED ? "IW_AUTH_PRIVACY_INVOKED" :
		"UNKNOWN",
		paramid, paramval));

#if defined(SOFTAP)
	if (ap_cfg_running) {
		WL_TRACE(("%s: Not executed, reason -'SOFTAP is active'\n", __FUNCTION__));
		return 0;
	}
#endif

	switch (paramid) {
	case IW_AUTH_WPA_VERSION:
		/* supported wpa version disabled or wpa or wpa2 */
		iw->wpaversion = paramval;
		break;

	case IW_AUTH_CIPHER_PAIRWISE:
		iw->pcipher = paramval;
		val = wl_iw_create_wpaauth_wsec(dev);
		if ((error = dev_wlc_intvar_set(dev, "wsec", val)))
			return error;
		break;

	case IW_AUTH_CIPHER_GROUP:
		iw->gcipher = paramval;
		val = wl_iw_create_wpaauth_wsec(dev);
		if ((error = dev_wlc_intvar_set(dev, "wsec", val)))
			return error;
		break;

	case IW_AUTH_KEY_MGMT:
		if (paramval & IW_AUTH_KEY_MGMT_PSK) {
			if (iw->wpaversion == IW_AUTH_WPA_VERSION_WPA)
				val = WPA_AUTH_PSK;
			else if (iw->wpaversion == IW_AUTH_WPA_VERSION_WPA2)
				val = WPA2_AUTH_PSK;
			else /* IW_AUTH_WPA_VERSION_DISABLED */
				val = WPA_AUTH_DISABLED;
		} else if (paramval & IW_AUTH_KEY_MGMT_802_1X) {
			if (iw->wpaversion == IW_AUTH_WPA_VERSION_WPA)
				val = WPA_AUTH_UNSPECIFIED;
			else if (iw->wpaversion == IW_AUTH_WPA_VERSION_WPA2)
				val = WPA2_AUTH_UNSPECIFIED;
			else /* IW_AUTH_WPA_VERSION_DISABLED */
				val = WPA_AUTH_DISABLED;
		}
		else
			val = WPA_AUTH_DISABLED;

		WL_INFORM(("%s: %d: setting wpa_auth to %d\n", __FUNCTION__, __LINE__, val));
		if ((error = dev_wlc_intvar_set(dev, "wpa_auth", val)))
			return error;
		break;

	case IW_AUTH_TKIP_COUNTERMEASURES:
		dev_wlc_bufvar_set(dev, "tkip_countermeasures", (char *)&paramval, 1);
		break;

	case IW_AUTH_80211_AUTH_ALG:
		/* open shared */
		WL_INFORM(("Setting the D11auth %d\n", paramval));
		if (paramval == IW_AUTH_ALG_OPEN_SYSTEM)
			val = 0;
		else if (paramval == IW_AUTH_ALG_SHARED_KEY)
			val = 1;
		else if (paramval == (IW_AUTH_ALG_OPEN_SYSTEM | IW_AUTH_ALG_SHARED_KEY))
			val = 2;
		else
			error = 1;
		if (!error && (error = dev_wlc_intvar_set(dev, "auth", val)))
			return error;
		break;

	case IW_AUTH_WPA_ENABLED:
		if (paramval == 0) {
			iw->privacy_invoked = 0; /* FALSE */
			iw->pcipher = 0;
			iw->gcipher = 0;
			val = wl_iw_create_wpaauth_wsec(dev);
			if ((error = dev_wlc_intvar_set(dev, "wsec", val)))
				return error;
			WL_INFORM(("%s: %d: setting wpa_auth to %d, wsec to %d\n",
				__FUNCTION__, __LINE__, paramval, val));
			dev_wlc_intvar_set(dev, "wpa_auth", paramval);
			return error;
		}

		/* nothing really needs to be done if wpa_auth enabled */
		break;

	case IW_AUTH_DROP_UNENCRYPTED:
		if ((error = dev_wlc_intvar_set(dev, "wsec_restrict", paramval)))
			return error;
		break;

	case IW_AUTH_RX_UNENCRYPTED_EAPOL:
		dev_wlc_bufvar_set(dev, "rx_unencrypted_eapol", (char *)&paramval, 1);
		break;

#if WIRELESS_EXT > 17
	case IW_AUTH_ROAMING_CONTROL:
		WL_INFORM(("%s: IW_AUTH_ROAMING_CONTROL\n", __FUNCTION__));
		/* driver control or user space app control */
		break;

	case IW_AUTH_PRIVACY_INVOKED:
		iw->privacy_invoked = paramval;
		val = wl_iw_create_wpaauth_wsec(dev);
		if ((error = dev_wlc_intvar_set(dev, "wsec", val)))
			return error;
		break;

#endif /* WIRELESS_EXT > 17 */
	default:
		break;
	}
	return 0;
}
#define VAL_PSK(_val) (((_val) & WPA_AUTH_PSK) || ((_val) & WPA2_AUTH_PSK))

static int
wl_iw_get_wpaauth(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_param *vwrq,
	char *extra
)
{
	int error;
	int paramid;
	int paramval = 0;
	int val;
	wl_iw_t *iw = NETDEV_PRIV(dev);

	WL_TRACE(("%s: SIOCGIWAUTH\n", dev->name));

	paramid = vwrq->flags & IW_AUTH_INDEX;

	switch (paramid) {
	case IW_AUTH_WPA_VERSION:
		paramval = iw->wpaversion;
		break;

	case IW_AUTH_CIPHER_PAIRWISE:
		paramval = iw->pcipher;
		break;

	case IW_AUTH_CIPHER_GROUP:
		paramval = iw->gcipher;
		break;

	case IW_AUTH_KEY_MGMT:
		/* psk, 1x */
		if ((error = dev_wlc_intvar_get(dev, "wpa_auth", &val)))
			return error;
		if (VAL_PSK(val))
			paramval = IW_AUTH_KEY_MGMT_PSK;
		else
			paramval = IW_AUTH_KEY_MGMT_802_1X;

		break;

	case IW_AUTH_TKIP_COUNTERMEASURES:
		dev_wlc_bufvar_get(dev, "tkip_countermeasures", (char *)&paramval, 1);
		break;

	case IW_AUTH_DROP_UNENCRYPTED:
		dev_wlc_intvar_get(dev, "wsec_restrict", &paramval);
		break;

	case IW_AUTH_RX_UNENCRYPTED_EAPOL:
		dev_wlc_bufvar_get(dev, "rx_unencrypted_eapol", (char *)&paramval, 1);
		break;

	case IW_AUTH_80211_AUTH_ALG:
		/* open, shared, leap */
		if ((error = dev_wlc_intvar_get(dev, "auth", &val)))
			return error;
		if (!val)
			paramval = IW_AUTH_ALG_OPEN_SYSTEM;
		else
			paramval = IW_AUTH_ALG_SHARED_KEY;
		break;
	case IW_AUTH_WPA_ENABLED:
		if ((error = dev_wlc_intvar_get(dev, "wpa_auth", &val)))
			return error;
		if (val)
			paramval = TRUE;
		else
			paramval = FALSE;
		break;
#if WIRELESS_EXT > 17
	case IW_AUTH_ROAMING_CONTROL:
		WL_ERROR(("%s: IW_AUTH_ROAMING_CONTROL\n", __FUNCTION__));
		/* driver control or user space app control */
		break;
	case IW_AUTH_PRIVACY_INVOKED:
		paramval = iw->privacy_invoked;
		break;

#endif /* WIRELESS_EXT > 17 */
	}
	vwrq->value = paramval;
	return 0;
}
#endif /* WIRELESS_EXT > 17 */


#ifdef SOFTAP
/*
*  SoftAP IOCTL wrapper implemention
*/
static int ap_macmode = MACLIST_MODE_DISABLED;
static struct mflist ap_black_list;

static int
wl_iw_parse_wep(char *keystr, wl_wsec_key_t *key)
{
	char hex[] = "XX";
	unsigned char *data = key->data;

	switch (strlen(keystr)) {
	case 5:
	case 13:
	case 16:
		key->len = strlen(keystr);
		memcpy(data, keystr, key->len + 1);
		break;
	case 12:
	case 28:
	case 34:
	case 66:
		/* strip leading 0x */
		if (!strnicmp(keystr, "0x", 2))
			keystr += 2;
		else
			return -1;
		/* fall through */
	case 10:
	case 26:
	case 32:
	case 64:
		key->len = strlen(keystr) / 2;
		while (*keystr) {
			strncpy(hex, keystr, 2);
			*data++ = (char) bcm_strtoul(hex, NULL, 16);
			keystr += 2;
		}
		break;
	default:
		return -1;
	}

	switch (key->len) {
	case 5:
		key->algo = CRYPTO_ALGO_WEP1;
		break;
	case 13:
		key->algo = CRYPTO_ALGO_WEP128;
		break;
	case 16:
		/* default to AES-CCM */
		key->algo = CRYPTO_ALGO_AES_CCM;
		break;
	case 32:
		key->algo = CRYPTO_ALGO_TKIP;
		break;
	default:
		return -1;
	}

	/* Set as primary key by default */
	key->flags |= WL_PRIMARY_KEY;

	return 0;
}

#ifdef EXT_WPA_CRYPTO
#define SHA1HashSize 20
extern void pbkdf2_sha1(const char *passphrase, const char *ssid, size_t ssid_len,
                        int iterations, u8 *buf, size_t buflen);

#else

#define SHA1HashSize 20
static int
pbkdf2_sha1(const char *passphrase, const char *ssid, size_t ssid_len,
            int iterations, u8 *buf, size_t buflen)
{
	WL_ERROR(("WARNING: %s is not implemented !!!\n", __FUNCTION__));
	return -1;
}

#endif /* EXT_WPA_CRYPTO */

/*  turn on the bss for the configuration 1 */
static int
dev_iw_write_cfg1_bss_var(struct net_device *dev, int val)
{
	struct {
		int cfg;
		int val;
	} bss_setbuf;

	int bss_set_res;
	char smbuf[WLC_IOCTL_SMLEN];
	memset(smbuf, 0, sizeof(smbuf));

	bss_setbuf.cfg = 1;
	bss_setbuf.val = val;

	bss_set_res = dev_iw_iovar_setbuf(dev, "bss",
		&bss_setbuf, sizeof(bss_setbuf), smbuf, sizeof(smbuf));
	WL_TRACE(("%s: bss_set_result:%d set with %d\n", __FUNCTION__, bss_set_res, val));

	return bss_set_res;
}


/*
*   formats an ioctl buffer  for accessing named variables
*   in the wl dongle by bssid index
*   returns total iolen
*/
#ifndef AP_ONLY
static int
wl_bssiovar_mkbuf(
		const char *iovar,
		int bssidx,
		void *param,
		int paramlen,
		void *bufptr,
		int buflen,
		int *perr)
{
	const char *prefix = "bsscfg:";
	int8* p;
	uint prefixlen;
	uint namelen;
	uint iolen;

	prefixlen = strlen(prefix);	/* length of bsscfg prefix */
	namelen = strlen(iovar) + 1;	/* length of iovar name + null */
	iolen = prefixlen + namelen + sizeof(int) + paramlen;

	/* check for overflow */
	if (buflen < 0 || iolen > (uint)buflen) {
		*perr = BCME_BUFTOOSHORT;
		return 0;
	}

	p = (int8*)bufptr;

	/* copy prefix, no null */
	memcpy(p, prefix, prefixlen);
	p += prefixlen;

	/* copy iovar name including null */
	memcpy(p, iovar, namelen);
	p += namelen;

	/* bss config index as first param */
	bssidx = htod32(bssidx);
	memcpy(p, &bssidx, sizeof(int32));
	p += sizeof(int32);

	/* parameter buffer follows */
	if (paramlen)
		memcpy(p, param, paramlen);

	*perr = 0;
	return iolen;
}
#endif /* AP_ONLY */


/*
*      *********  IWPRIV interface only   *******
*/

#define strtoul(nptr, endptr, base) bcm_strtoul((nptr), (endptr), (base))

/*
 * == Combo Scan implemenation : allowed multi-SSIDs scan in one call ==
 */
#if defined(CSCAN)


/* Function to execute combined scan */
static int
wl_iw_combined_scan_set(struct net_device *dev, wlc_ssid_t* ssids_local, int nssid, int nchan)
{
	int params_size = WL_SCAN_PARAMS_FIXED_SIZE + WL_NUMCHANNELS * sizeof(uint16);
	int err = 0;
	char *p;
	int i;
	iscan_info_t *iscan = g_iscan;

	WL_TRACE(("%s nssid=%d nchan=%d\n", __FUNCTION__, nssid, nchan));

	if ((!dev) && (!g_iscan) && (!iscan->iscan_ex_params_p)) {
		WL_ERROR(("%s error exit\n", __FUNCTION__));
		err = -1;
		goto exit;
	}

#ifdef PNO_SUPPORT
	/* maybe better ignore scan request when PNO is still active */
	if  (dhd_dev_get_pno_status(dev)) {
		WL_ERROR(("%s: Scan called when PNO is active\n", __FUNCTION__));
	}
#endif /* PNO_SUPPORT */

	params_size += WL_SCAN_PARAMS_SSID_MAX * sizeof(wlc_ssid_t);

	/* Copy ssid array if applicable */
	if (nssid > 0) {
		i = OFFSETOF(wl_scan_params_t, channel_list) + nchan * sizeof(uint16);
		i = ROUNDUP(i, sizeof(uint32));
		if (i + nssid * sizeof(wlc_ssid_t) > params_size) {
			printf("additional ssids exceed params_size\n");
			err = -1;
			goto exit;
		}

		p = ((char*)&iscan->iscan_ex_params_p->params) + i;
		memcpy(p, ssids_local, nssid * sizeof(wlc_ssid_t));
		p += nssid * sizeof(wlc_ssid_t);
	} else {
		p = (char*)iscan->iscan_ex_params_p->params.channel_list + nchan * sizeof(uint16);
	}

	/* adding mask to channel numbers */
	iscan->iscan_ex_params_p->params.channel_num =
	        htod32((nssid << WL_SCAN_PARAMS_NSSID_SHIFT) |
	               (nchan & WL_SCAN_PARAMS_COUNT_MASK));

	nssid = (uint)
	        ((iscan->iscan_ex_params_p->params.channel_num >> WL_SCAN_PARAMS_NSSID_SHIFT) &
	         WL_SCAN_PARAMS_COUNT_MASK);

	/* adjust param size */
	params_size = (int) (p - (char*)iscan->iscan_ex_params_p + nssid * sizeof(wlc_ssid_t));
	iscan->iscan_ex_param_size = params_size;

	iscan->list_cur = iscan->list_hdr;
	iscan->iscan_state = ISCAN_STATE_SCANING;
	wl_iw_set_event_mask(dev);
	mod_timer(&iscan->timer, jiffies + iscan->timer_ms*HZ/1000);

	iscan->timer_on = 1;

#ifdef SCAN_DUMP
	{
		int i;
		WL_SCAN(("\n### List of SSIDs to scan ###\n"));
		for (i = 0; i < nssid; i++) {
			if (!ssids_local[i].SSID_len)
				WL_SCAN(("%d: Broadcast scan\n", i));
			else
			WL_SCAN(("%d: scan  for  %s size =%d\n", i,
				ssids_local[i].SSID, ssids_local[i].SSID_len));
		}
		WL_SCAN(("### List of channels to scan ###\n"));
		for (i = 0; i < nchan; i++)
		{
			WL_SCAN(("%d ", iscan->iscan_ex_params_p->params.channel_list[i]));
		}
		WL_SCAN(("\nnprobes=%d\n", iscan->iscan_ex_params_p->params.nprobes));
		WL_SCAN(("active_time=%d\n", iscan->iscan_ex_params_p->params.active_time));
		WL_SCAN(("passive_time=%d\n", iscan->iscan_ex_params_p->params.passive_time));
		WL_SCAN(("home_time=%d\n", iscan->iscan_ex_params_p->params.home_time));
		WL_SCAN(("scan_type=%d\n", iscan->iscan_ex_params_p->params.scan_type));
		WL_SCAN(("\n###################\n"));
	}
#endif /* SCAN_DUMP */

	if (params_size > WLC_IOCTL_MEDLEN) {
			WL_ERROR(("Set ISCAN for %s due to params_size=%d  \n",
				__FUNCTION__, params_size));
			err = -1;
	}

	if ((err = dev_iw_iovar_setbuf(dev, "iscan", iscan->iscan_ex_params_p,
	                               iscan->iscan_ex_param_size,
	                               iscan->ioctlbuf, sizeof(iscan->ioctlbuf)))) {
		WL_TRACE(("Set ISCAN for %s failed with %d\n", __FUNCTION__, err));
		err = -1;
	}

exit:
	return err;
}

/*
*  Combined scan executed scan per specified SSIDs ("" for Broadcast or Specified SSID)
*  per specified channels ("" for ALL channels)
*  per specified number of probes and specified scan time
*  Maybe called from Linux open source iwpriv tool/
*  iwpriv eth0 CSCAN SSID="","SSID#1",CH=1,NPROBE="2",ACTIVE="200",PASSIVE="",HOME="200"
*/
static int
iwpriv_set_cscan(struct net_device *dev, struct iw_request_info *info,
                 union iwreq_data *wrqu, char *ext)
{
	int res;
	char  *extra = NULL;
	iscan_info_t *iscan = g_iscan;
	wlc_ssid_t ssids_local[WL_SCAN_PARAMS_SSID_MAX];
	int nssid;
	int nchan;
	char *str_ptr;

	WL_TRACE(("%s: info->cmd:%x, info->flags:%x, u.data=0x%p, u.len=%d\n",
		__FUNCTION__, info->cmd, info->flags,
		wrqu->data.pointer, wrqu->data.length));

	if (g_onoff == G_WLAN_SET_OFF) {
		WL_TRACE(("%s: driver is not up yet after START\n", __FUNCTION__));
		return -ENODEV;
	}

	if (wrqu->data.length == 0) {
		WL_ERROR(("IWPRIV argument len = 0\n"));
		return -EINVAL;
	}

	if (!iscan->iscan_ex_params_p) {
		return -EFAULT;
	}

	if (!(extra = kmalloc(wrqu->data.length+1, GFP_KERNEL)))
		return -ENOMEM;

	if (copy_from_user(extra, wrqu->data.pointer, wrqu->data.length)) {
		res = -EFAULT;
		goto exit_proc;
	}

	extra[wrqu->data.length] = 0;
	WL_ERROR(("Got str param in iw_point:\n %s\n", extra));

	str_ptr = extra;

	/* Search for SSIDs info */
	if (strncmp(str_ptr, GET_SSID, strlen(GET_SSID))) {
		WL_ERROR(("%s Error: extracting SSID='' string\n", __FUNCTION__));
		res = -EINVAL;
		goto exit_proc;
	}

	str_ptr += strlen(GET_SSID);
	nssid = wl_iw_parse_ssid_list(&str_ptr, ssids_local, nssid,
	                              WL_SCAN_PARAMS_SSID_MAX);
	if (nssid == -1) {
		WL_ERROR(("%s wrong ssid list", __FUNCTION__));
		res = -EINVAL;
		goto exit_proc;
	}

	memset(iscan->iscan_ex_params_p, 0, iscan->iscan_ex_param_size);
	ASSERT(iscan->iscan_ex_param_size < WLC_IOCTL_MAXLEN);

	/* Set all default params */
	wl_iw_iscan_prep(&iscan->iscan_ex_params_p->params, NULL);
	iscan->iscan_ex_params_p->version = htod32(ISCAN_REQ_VERSION);
	iscan->iscan_ex_params_p->action = htod16(WL_SCAN_ACTION_START);
	iscan->iscan_ex_params_p->scan_duration = htod16(0);

	/* Search for channel info */
	if ((nchan = wl_iw_parse_channel_list(&str_ptr,
	                                      &iscan->iscan_ex_params_p->params.channel_list[0],
	                                      WL_NUMCHANNELS)) == -1) {
		WL_ERROR(("%s missing channel list\n", __FUNCTION__));
		res = -EINVAL;
		goto exit_proc;
	}

	/* Get scanning dwell time */
	get_parameter_from_string(&str_ptr,
	                          GET_NPROBE, PTYPE_INTDEC,
	                          &iscan->iscan_ex_params_p->params.nprobes, 2);

	get_parameter_from_string(&str_ptr, GET_ACTIVE_ASSOC_DWELL, PTYPE_INTDEC,
	                          &iscan->iscan_ex_params_p->params.active_time, 4);

	get_parameter_from_string(&str_ptr, GET_PASSIVE_ASSOC_DWELL, PTYPE_INTDEC,
	                          &iscan->iscan_ex_params_p->params.passive_time, 4);

	get_parameter_from_string(&str_ptr, GET_HOME_DWELL, PTYPE_INTDEC,
	                          &iscan->iscan_ex_params_p->params.home_time, 4);

	get_parameter_from_string(&str_ptr, GET_SCAN_TYPE, PTYPE_INTDEC,
	                          &iscan->iscan_ex_params_p->params.scan_type, 1);

	/* Combined SCAN execution */
	res = wl_iw_combined_scan_set(dev, ssids_local, nssid, nchan);

exit_proc:
	kfree(extra);

	return res;
}

/*
* ComboScan TLV-based request
* Called from WPA_SUPPLICANT if implemented and supported by wpa_supplicant
* WPA_SUPPLICANT sends TLV based ComboSCAN request with SSID lists, channel to scan,
* numner of probe request, active/passive/home dwell time per.
* Return :
*	0   if parsed and executed comboScan
*     -1 othwerwise
*/
static int
wl_iw_set_cscan(
	struct net_device *dev,
	struct iw_request_info *info,
	union iwreq_data *wrqu,
	char *extra
)
{
	int res = -1;
	iscan_info_t *iscan = g_iscan;
	wlc_ssid_t ssids_local[WL_SCAN_PARAMS_SSID_MAX];
	int nssid = 0;
	int nchan = 0;
	cscan_tlv_t *cscan_tlv_temp;
	char type;
	char *str_ptr;
	int tlv_size_left;
#ifdef TLV_DEBUG
	int i;
	char tlv_in_example[] = {
		'C', 'S', 'C', 'A', 'N', ' ',
		0x53, 0x01, 0x00, 0x00,
		'S',	  /* SSID type */
		0x00, /* SSID Broadcast */
		'S',    /* SSID type */
		0x04, /* SSID size */
		'B', 'R', 'C', 'M',
		'C',
		0x06, /* channel 6 */
		'P', /*  Passive dwell  0x1194=4500 msec */
		0x94,
		0x11,
		'T',     /* Scan type */
		0x01  /* Passive */
	};
#endif /* TLV_DEBUG */

	WL_TRACE(("\n### %s: info->cmd:%x, info->flags:%x, u.data=0x%p, u.len=%d\n",
		__FUNCTION__, info->cmd, info->flags,
		wrqu->data.pointer, wrqu->data.length));

	net_os_wake_lock(dev);

	if (g_onoff == G_WLAN_SET_OFF) {
		WL_TRACE(("%s: driver is not up yet after START\n", __FUNCTION__));
		goto exit_proc;
	}

	if (wrqu->data.length < (strlen(CSCAN_COMMAND) + sizeof(cscan_tlv_t))) {
		WL_ERROR(("%s aggument=%d  less %d\n", __FUNCTION__,
			wrqu->data.length, strlen(CSCAN_COMMAND) + sizeof(cscan_tlv_t)));
		goto exit_proc;
	}

#ifdef TLV_DEBUG
	memcpy(extra, tlv_in_example, sizeof(tlv_in_example));
	wrqu->data.length = sizeof(tlv_in_example);
	for (i = 0; i < wrqu->data.length; i++)
		printf("%02X ", extra[i]);
	printf("\n");
#endif /* TLV_DEBUG */

	str_ptr = extra;
	str_ptr +=  strlen(CSCAN_COMMAND);
	tlv_size_left = wrqu->data.length - strlen(CSCAN_COMMAND);

	cscan_tlv_temp = (cscan_tlv_t *)str_ptr;
	memset(ssids_local, 0, sizeof(ssids_local));

	/* CSCAN TLV command must started with predefined
	 * prefixes and first parameter should be for SSID
	 */
	if ((cscan_tlv_temp->prefix != CSCAN_TLV_PREFIX) ||
	    (cscan_tlv_temp->version != CSCAN_TLV_VERSION) ||
	    (cscan_tlv_temp->subver != CSCAN_TLV_SUBVERSION)) {
		WL_ERROR(("%s get wrong TLV command\n", __FUNCTION__));
		goto exit_proc;
	}

	str_ptr += sizeof(cscan_tlv_t);
	tlv_size_left  -= sizeof(cscan_tlv_t);

	/* Get SSIDs list */
	if ((nssid = wl_iw_parse_ssid_list_tlv(&str_ptr, ssids_local,
	                                       WL_SCAN_PARAMS_SSID_MAX,
	                                       &tlv_size_left)) <= 0) {
		WL_ERROR(("SSID is not presented or corrupted ret=%d\n", nssid));
		goto exit_proc;
	}

	/* Adjust size to cached ssids */
	memset(iscan->iscan_ex_params_p, 0, iscan->iscan_ex_param_size);

	/* Set all default params */
	wl_iw_iscan_prep(&iscan->iscan_ex_params_p->params, NULL);
	iscan->iscan_ex_params_p->version = htod32(ISCAN_REQ_VERSION);
	iscan->iscan_ex_params_p->action = htod16(WL_SCAN_ACTION_START);
	iscan->iscan_ex_params_p->scan_duration = htod16(0);

	/* Get other params */
	while (tlv_size_left > 0) {
		type = str_ptr[0];
		switch (type) {
		case CSCAN_TLV_TYPE_CHANNEL_IE:
			/* Search for channel info */
			if ((nchan = wl_iw_parse_channel_list_tlv
			     (&str_ptr,
			      &iscan->iscan_ex_params_p->params.channel_list[0],
			      WL_NUMCHANNELS, &tlv_size_left)) == -1) {
				WL_ERROR(("%s missing channel list\n",
				          __FUNCTION__));
				goto exit_proc;
			}
			break;
		case CSCAN_TLV_TYPE_NPROBE_IE:
			if ((res = wl_iw_parse_data_tlv
			     (&str_ptr,
			      &iscan->iscan_ex_params_p->params.nprobes,
			      sizeof(iscan->iscan_ex_params_p->params.nprobes),
			      type, sizeof(char), &tlv_size_left)) == -1) {
				WL_ERROR(("%s return %d\n",
				          __FUNCTION__, res));
				goto exit_proc;
			}
			break;
		case CSCAN_TLV_TYPE_ACTIVE_IE:
			if ((res = wl_iw_parse_data_tlv
			     (&str_ptr,
			      &iscan->iscan_ex_params_p->params.active_time,
			      sizeof(iscan->iscan_ex_params_p->params.active_time),
			      type, sizeof(short), &tlv_size_left)) == -1) {
				WL_ERROR(("%s return %d\n",
				          __FUNCTION__, res));
				goto exit_proc;
			}
			break;
		case CSCAN_TLV_TYPE_PASSIVE_IE:
			if ((res = wl_iw_parse_data_tlv
			     (&str_ptr,
			      &iscan->iscan_ex_params_p->params.passive_time,
			      sizeof(iscan->iscan_ex_params_p->params.passive_time),
			      type, sizeof(short), &tlv_size_left)) == -1) {
				WL_ERROR(("%s return %d\n",
				          __FUNCTION__, res));
				goto exit_proc;
			}
			break;
		case CSCAN_TLV_TYPE_HOME_IE:
			if ((res = wl_iw_parse_data_tlv
			     (&str_ptr,
			      &iscan->iscan_ex_params_p->params.home_time,
			      sizeof(iscan->iscan_ex_params_p->params.home_time),
			      type, sizeof(short), &tlv_size_left)) == -1) {
				WL_ERROR(("%s return %d\n",
				          __FUNCTION__, res));
				goto exit_proc;
			}
			break;
		case CSCAN_TLV_TYPE_STYPE_IE:
			if ((res = wl_iw_parse_data_tlv
			     (&str_ptr,
			      &iscan->iscan_ex_params_p->params.scan_type,
			      sizeof(iscan->iscan_ex_params_p->params.scan_type),
			      type, sizeof(char), &tlv_size_left)) == -1) {
				WL_ERROR(("%s return %d\n",
				          __FUNCTION__, res));
				goto exit_proc;
			}
			break;
		default:
			WL_ERROR(("%s get unkwown type %X\n",
			          __FUNCTION__, type));
			goto exit_proc;
			break;
		}
	}

	if (g_first_broadcast_scan < BROADCAST_SCAN_FIRST_RESULT_CONSUMED) {
		if (++g_first_counter_scans != MAX_ALLOWED_BLOCK_SCAN_FROM_FIRST_SCAN) {
			WL_ERROR(("%s Ignoring CSCAN: First Scan is not done yet %d\n",
			          __FUNCTION__, g_first_counter_scans));
			res = -EBUSY;
			goto exit_proc;
		}

		WL_ERROR(("%s Clean up First scan flag which is %d\n",
		          __FUNCTION__, g_first_broadcast_scan));
		g_first_broadcast_scan = BROADCAST_SCAN_FIRST_RESULT_CONSUMED;
	}

	/* Combined SCAN execution */
	res = wl_iw_combined_scan_set(dev, ssids_local, nssid, nchan);

exit_proc:
	net_os_wake_unlock(dev);
	return res;
}

#endif 


/***************** SOFT AP implemenation  *******************
*/
#ifdef SOFTAP
#ifndef AP_ONLY

/*
*  **************** thread that waits until SOFT_AP wl0.1 eth dev is created *******************
*/
static int
thr_wait_for_2nd_eth_dev(void *data)
{
	int ret = 0;

	DAEMONIZE("wl0_eth_wthread");

	WL_TRACE(("%s thread started:, PID:%x\n", __FUNCTION__, current->pid));

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
	if (down_timeout(&ap_eth_sema,  msecs_to_jiffies(5000)) != 0) {
#else
	if (down_interruptible(&ap_eth_sema) != 0) {
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27) */
		WL_ERROR(("\n%s: sap_eth_sema timeout \n", __FUNCTION__));
		ret = -1;
		goto fail;
	}

	if (!ap_net_dev) {
		WL_ERROR((" ap_net_dev is null !!!"));
		ret = -1;
		goto fail;
	}

	WL_TRACE(("\n>%s: Thread:'softap ethdev IF:%s is detected !!!'\n\n",
		__FUNCTION__, ap_net_dev->name));

	ap_cfg_running = TRUE;

	bcm_mdelay(500); /* don't remove , we need this delay */

	/* this event will be relayed back as a command so we could start the AP service */
	wl_iw_send_priv_event(priv_dev, "AP_SET_CFG_OK");

fail:

	WL_TRACE(("\n>%s, thread completed\n", __FUNCTION__));

	return ret;
}
#endif /* AP_ONLY */
#ifndef AP_ONLY
static int last_auto_channel = 6;
#endif
static int
get_softap_auto_channel(struct net_device *dev, struct ap_profile *ap)
{
	int chosen = 0;
	wl_uint32_list_t request;
	int rescan = 0;
	int retry = 0;
	int updown = 0;
	int ret = 0;
	wlc_ssid_t null_ssid;
	int res = 0;
#ifndef AP_ONLY
	int iolen = 0;
	int mkvar_err = 0;
	int bsscfg_index = 1;
	char buf[WLC_IOCTL_SMLEN];
#endif
	WL_SOFTAP(("Enter %s\n", __FUNCTION__));

#ifndef AP_ONLY
	if (ap_cfg_running) {
		ap->channel = last_auto_channel;
		return res;
	}
#endif
	memset(&null_ssid, 0, sizeof(wlc_ssid_t));
	res |= dev_wlc_ioctl(dev, WLC_UP, &updown, sizeof(updown));
#ifdef AP_ONLY
	res |= dev_wlc_ioctl(dev, WLC_SET_SSID, &null_ssid, sizeof(null_ssid));
#else
	iolen = wl_bssiovar_mkbuf("ssid", bsscfg_index, (char *)(&null_ssid),
		null_ssid.SSID_len+4, buf, sizeof(buf), &mkvar_err);
	ASSERT(iolen);
	res |= dev_wlc_ioctl(dev, WLC_SET_VAR, buf, iolen);
#endif
	auto_channel_retry:
			request.count = htod32(0);
			ret = dev_wlc_ioctl(dev, WLC_START_CHANNEL_SEL, &request, sizeof(request));
			if (ret < 0) {
				WL_ERROR(("can't start auto channel scan\n"));
				goto fail;
			}

	get_channel_retry:
			bcm_mdelay(500);

			ret = dev_wlc_ioctl(dev, WLC_GET_CHANNEL_SEL, &chosen, sizeof(chosen));
			if (ret < 0 || dtoh32(chosen) == 0) {
				if (retry++ < 3)
					goto get_channel_retry;
				else {
					WL_ERROR(("can't get auto channel sel, err = %d, "
					          "chosen = %d\n", ret, chosen));
					goto fail;
				}
			}
			if ((chosen == 1) && (!rescan++))
				goto auto_channel_retry;
			WL_SOFTAP(("Set auto channel = %d\n", chosen));
			ap->channel = chosen;
			if ((res = dev_wlc_ioctl(dev, WLC_DOWN, &updown, sizeof(updown))) < 0) {
				WL_ERROR(("%s fail to set up err =%d\n", __FUNCTION__, res));
				goto fail;
			}
#ifndef AP_ONLY
	if (!res)
		last_auto_channel = ap->channel;
#endif

fail :
	return res;
} /* ap channel autosellect */

/*
*  ********* starts SOFT_AP configuration process *********
*/
static int
set_ap_cfg(struct net_device *dev, struct ap_profile *ap)
{
	int updown = 0;
	int channel = 0;

	wlc_ssid_t ap_ssid;
	int max_assoc = 8;

	int res = 0;
	int apsta_var = 0;
#ifndef AP_ONLY
	int mpc = 0;
	int iolen = 0;
	int mkvar_err = 0;
	int bsscfg_index = 1;
	char buf[WLC_IOCTL_SMLEN];
#endif

	if (!dev) {
		WL_ERROR(("%s: dev is null\n", __FUNCTION__));
		return -1;
	}

	net_os_wake_lock(dev);
	DHD_OS_MUTEX_LOCK(&wl_softap_lock);

	WL_SOFTAP(("wl_iw: set ap profile:\n"));
	WL_SOFTAP(("	ssid = '%s'\n", ap->ssid));
	WL_SOFTAP(("	security = '%s'\n", ap->sec));
	if (ap->key[0] != '\0')
		WL_SOFTAP(("	key = '%s'\n", ap->key));
	WL_SOFTAP(("	channel = %d\n", ap->channel));
	WL_SOFTAP(("	max scb = %d\n", ap->max_scb));

#ifdef AP_ONLY
	if (ap_cfg_running) {
		wl_iw_softap_deassoc_stations(dev);
		ap_cfg_running = FALSE;
	}
#endif	/* AP_ONLY */

	/* skip certain operations if AP interface already exists  */
	if (ap_cfg_running == FALSE) {

#ifndef AP_ONLY

		/* init wl0.1 "created"  semaphore */
		sema_init(&ap_eth_sema, 0);

		mpc = 0;
		if ((res = dev_wlc_intvar_set(dev, "mpc", mpc))) {
			WL_ERROR(("%s fail to set mpc\n", __FUNCTION__));
			goto fail;
		}
#endif

		updown = 0;
		if ((res = dev_wlc_ioctl(dev, WLC_DOWN, &updown, sizeof(updown)))) {
			WL_ERROR(("%s fail to set updown\n", __FUNCTION__));
			goto fail;
		}

#ifdef AP_ONLY
		/* configure as AP only mode ( single interface ???)  */
		apsta_var = 0;
		if ((res = dev_wlc_ioctl(dev, WLC_SET_AP, &apsta_var, sizeof(apsta_var)))) {
			WL_ERROR(("%s fail to set apsta_var 0\n", __FUNCTION__));
			goto fail;
		}
		apsta_var = 1;
		if ((res = dev_wlc_ioctl(dev, WLC_SET_AP, &apsta_var, sizeof(apsta_var)))) {
			WL_ERROR(("%s fail to set apsta_var 1\n", __FUNCTION__));
			goto fail;
		}
		res = dev_wlc_ioctl(dev, WLC_GET_AP, &apsta_var, sizeof(apsta_var));
#else
		/*   APSTA MODE ( default ) 2 net_device interfaces */
		apsta_var = 1;
		iolen = wl_bssiovar_mkbuf("apsta",
			bsscfg_index,  &apsta_var, sizeof(apsta_var)+4,
			buf, sizeof(buf), &mkvar_err);
		ASSERT(iolen);
		if ((res = dev_wlc_ioctl(dev, WLC_SET_VAR, buf, iolen)) < 0) {
			WL_ERROR(("%s fail to set apsta \n", __FUNCTION__));
			goto fail;
		}
		WL_TRACE(("\n>in %s: apsta set result: %d \n", __FUNCTION__, res));
#endif /* AP_ONLY */

		updown = 1;
		if ((res = dev_wlc_ioctl(dev, WLC_UP, &updown, sizeof(updown))) < 0) {
			WL_ERROR(("%s fail to set apsta \n", __FUNCTION__));
			goto fail;
		}

	} else {
		/* if SoftAP is already running */
		if (!ap_net_dev) {
			WL_ERROR(("%s: ap_net_dev is null\n", __FUNCTION__));
			goto fail;
		}

		res = wl_iw_softap_deassoc_stations(ap_net_dev);

		/*  TURN Down AP BSS  */
		if ((res = dev_iw_write_cfg1_bss_var(dev, 0)) < 0) {
			WL_ERROR(("%s fail to set bss down\n", __FUNCTION__));
			goto fail;
		}
	}

	/* ----  AP channel autoselect --- */
	if ((ap->channel == 0) && (get_softap_auto_channel(dev, ap) < 0)) {
		ap->channel = 1;
		WL_ERROR(("%s auto channel failed, pick up channel=%d\n",
		          __FUNCTION__, ap->channel));
	}

	channel = ap->channel;
	if ((res = dev_wlc_ioctl(dev, WLC_SET_CHANNEL, &channel, sizeof(channel)))) {
		WL_ERROR(("%s fail to set channel\n", __FUNCTION__));
		goto fail;
	}

	if (ap_cfg_running == FALSE) {
		updown = 0;
		if ((res = dev_wlc_ioctl(dev, WLC_UP, &updown, sizeof(updown)))) {
			WL_ERROR(("%s fail to set up\n", __FUNCTION__));
			goto fail;
		}
	}

	max_assoc = ap->max_scb;
	if ((res = dev_wlc_intvar_set(dev, "maxassoc", max_assoc))) {
		WL_ERROR(("%s fail to set maxassoc\n", __FUNCTION__));
		goto fail;
	}

	ap_ssid.SSID_len = strlen(ap->ssid);
	strncpy(ap_ssid.SSID, ap->ssid, ap_ssid.SSID_len);

	/*  wl method of setting dhd variables  */
#ifdef AP_ONLY
	if ((res = wl_iw_set_ap_security(dev, &my_ap)) != 0) {
		WL_ERROR(("ERROR:%d in:%s, wl_iw_set_ap_security is skipped\n",
		          res, __FUNCTION__));
		goto fail;
	}
	wl_iw_send_priv_event(dev, "ASCII_CMD=AP_BSS_START");
	ap_cfg_running = TRUE;
#else

	iolen = wl_bssiovar_mkbuf("ssid", bsscfg_index, (char *)(&ap_ssid),
		ap_ssid.SSID_len+4, buf, sizeof(buf), &mkvar_err);
	ASSERT(iolen);
	if ((res = dev_wlc_ioctl(dev, WLC_SET_VAR, buf, iolen)) != 0) {
		WL_ERROR(("ERROR:%d in:%s, Security & BSS reconfiguration is skipped\n",
		          res, __FUNCTION__));
		goto fail;
	}
	if (ap_cfg_running == FALSE) {
		/*
		 *  this thread will sleep until wl0.1(2nd eth dev) is created
		 *  and then will complete SOFTAP configuration process
		 */
		kernel_thread(thr_wait_for_2nd_eth_dev, 0, 0);
	} else {

		/* if  the AP interface wl0.1 already exists we call security & bss UP in here */
		if (ap_net_dev == NULL) {
			WL_ERROR(("%s ERROR: ap_net_dev is NULL !!!\n", __FUNCTION__));
			goto fail;
		}

		WL_ERROR(("%s: %s Configure security & restart AP bss \n",
		          __FUNCTION__, ap_net_dev->name));

		/* set all ap security from global val  */
		if ((res = wl_iw_set_ap_security(ap_net_dev, &my_ap)) < 0) {
			WL_ERROR(("%s fail to set security : %d\n", __FUNCTION__, res));
			goto fail;
		}

		/* kick off SOFTAP BSS */
		if ((res = dev_iw_write_cfg1_bss_var(dev, 1)) < 0) {
			WL_ERROR(("%s fail to set bss up\n", __FUNCTION__));
			goto fail;
		}
	}
#endif /* AP_ONLY */
fail:
	WL_SOFTAP(("%s exit with %d\n", __FUNCTION__, res));

	DHD_OS_MUTEX_UNLOCK(&wl_softap_lock);
	net_os_wake_unlock(dev);

	return res;
}
#endif /* SOFTAP */


/*
*      *********  set Soft AP security mode *******
*/
static int
wl_iw_set_ap_security(struct net_device *dev, struct ap_profile *ap)
{
	int wsec = 0;
	int wpa_auth = 0;
	int res = 0;
	int i;
	char *ptr;
#ifdef AP_ONLY
	int mpc = 0;
	wlc_ssid_t ap_ssid;
#endif
	wl_wsec_key_t key;

	WL_SOFTAP(("\nsetting SOFTAP security mode:\n"));
	WL_SOFTAP(("wl_iw: set ap profile:\n"));
	WL_SOFTAP(("	ssid = '%s'\n", ap->ssid));
	WL_SOFTAP(("	security = '%s'\n", ap->sec));
	if (ap->key[0] != '\0')
		WL_SOFTAP(("	key = '%s'\n", ap->key));
	WL_SOFTAP(("	channel = %d\n", ap->channel));
	WL_SOFTAP(("	max scb = %d\n", ap->max_scb));

	if (strnicmp(ap->sec, "open", strlen("open")) == 0) {

	   /* * ============  OPEN  =========== * */
		wsec = 0;
		res = dev_wlc_intvar_set(dev, "wsec", wsec);
		wpa_auth = WPA_AUTH_DISABLED;
		res |= dev_wlc_intvar_set(dev, "wpa_auth", wpa_auth);

		WL_SOFTAP(("=====================\n"));
		WL_SOFTAP((" wsec & wpa_auth set 'OPEN', result:&d %d\n", res));
		WL_SOFTAP(("=====================\n"));

	} else if (strnicmp(ap->sec, "wep", strlen("wep")) == 0) {

	   /* * ============  WEP  =========== * */
		memset(&key, 0, sizeof(key));

		wsec = WEP_ENABLED;
		res = dev_wlc_intvar_set(dev, "wsec", wsec);

		key.index = 0;
		if (wl_iw_parse_wep(ap->key, &key)) {
			WL_SOFTAP(("wep key parse err!\n"));
			return -1;
		}

		key.index = htod32(key.index);
		key.len = htod32(key.len);
		key.algo = htod32(key.algo);
		key.flags = htod32(key.flags);

		res |= dev_wlc_ioctl(dev, WLC_SET_KEY, &key, sizeof(key));

		wpa_auth = WPA_AUTH_DISABLED;
		res |= dev_wlc_intvar_set(dev, "wpa_auth", wpa_auth);

		WL_SOFTAP(("=====================\n"));
		WL_SOFTAP((" wsec & auth set 'WEP', result:&d %d\n", res));
		WL_SOFTAP(("=====================\n"));

	} else if (strnicmp(ap->sec, "wpa2-psk", strlen("wpa2-psk")) == 0) {

	   /* * ==========  WPA2 AES  =========== * */

		wsec_pmk_t psk;
		size_t key_len;

		wsec = AES_ENABLED;
		dev_wlc_intvar_set(dev, "wsec", wsec);

		key_len = strlen(ap->key);
		if (key_len < WSEC_MIN_PSK_LEN || key_len > WSEC_MAX_PSK_LEN) {
			WL_SOFTAP(("passphrase must be between %d and %d characters long\n",
			WSEC_MIN_PSK_LEN, WSEC_MAX_PSK_LEN));
			return -1;
		}

		/*  turn all psk-key to 64 char wide in default. */
		if (key_len < WSEC_MAX_PSK_LEN) {
			unsigned char output[2*SHA1HashSize];
			char key_str_buf[WSEC_MAX_PSK_LEN+1];

			/*  passhash to make hash */
			memset(output, 0, sizeof(output));
			pbkdf2_sha1(ap->key, ap->ssid, strlen(ap->ssid), 4096, output, 32);
			/* anthony: turn hex digit to char string */
			ptr = key_str_buf;
			for (i = 0; i < (WSEC_MAX_PSK_LEN/8); i++) {
				/* anthony: the order is comfirmed in big-endian.
				 It maybe different in little-endian?
				*/
				sprintf(ptr, "%02x%02x%02x%02x", (uint)output[i*4],
				        (uint)output[i*4+1], (uint)output[i*4+2],
				        (uint)output[i*4+3]);
				ptr += 8;
			}
			WL_SOFTAP(("%s: passphase = %s\n", __FUNCTION__, key_str_buf));

			psk.key_len = htod16((ushort)WSEC_MAX_PSK_LEN);
			memcpy(psk.key, key_str_buf, psk.key_len);
		} else {
			psk.key_len = htod16((ushort) key_len);
			memcpy(psk.key, ap->key, key_len);
		}
		psk.flags = htod16(WSEC_PASSPHRASE);
		dev_wlc_ioctl(dev, WLC_SET_WSEC_PMK, &psk, sizeof(psk));

		wpa_auth = WPA2_AUTH_PSK;
		dev_wlc_intvar_set(dev, "wpa_auth", wpa_auth);

	} else if (strnicmp(ap->sec, "wpa-psk", strlen("wpa-psk")) == 0) {

		/* * ========  WPA TKIP ============ * */
		wsec_pmk_t psk;
		size_t key_len;

		wsec = TKIP_ENABLED;
		res = dev_wlc_intvar_set(dev, "wsec", wsec);

		key_len = strlen(ap->key);
		if (key_len < WSEC_MIN_PSK_LEN || key_len > WSEC_MAX_PSK_LEN) {
			WL_SOFTAP(("passphrase must be between %d and %d characters long\n",
			WSEC_MIN_PSK_LEN, WSEC_MAX_PSK_LEN));
			return -1;
		}

		/* anthony: turn all psk-key to 64 char wide in default. */
		if (key_len < WSEC_MAX_PSK_LEN) {
			unsigned char output[2*SHA1HashSize];
			char key_str_buf[WSEC_MAX_PSK_LEN+1];

			WL_SOFTAP(("%s: do passhash...\n", __FUNCTION__));
			/* call passhash to make hash */
			pbkdf2_sha1(ap->key, ap->ssid, strlen(ap->ssid), 4096, output, 32);
			/*  turn hex digit to char string */
			ptr = key_str_buf;
			for (i = 0; i < (WSEC_MAX_PSK_LEN/8); i++) {
				WL_SOFTAP(("[%02d]: %08x\n", i, *((unsigned int*)&output[i*4])));
				/* the order is comfirmed in big-endian.
				 * It maybe different in little-endian?
				 */
				sprintf(ptr, "%02x%02x%02x%02x", (uint)output[i*4],
					(uint)output[i*4+1], (uint)output[i*4+2],
				        (uint)output[i*4+3]);
				ptr += 8;
			}
			printk("%s: passphase = %s\n", __FUNCTION__, key_str_buf);

			psk.key_len = htod16((ushort)WSEC_MAX_PSK_LEN);
			memcpy(psk.key, key_str_buf, psk.key_len);
		} else {
			psk.key_len = htod16((ushort) key_len);
			memcpy(psk.key, ap->key, key_len);
		}

		psk.flags = htod16(WSEC_PASSPHRASE);
		res |= dev_wlc_ioctl(dev, WLC_SET_WSEC_PMK, &psk, sizeof(psk));

		wpa_auth = WPA_AUTH_PSK;
		res |= dev_wlc_intvar_set(dev, "wpa_auth", wpa_auth);

		WL_SOFTAP((" wsec & auth set 'wpa-psk' (TKIP), result:&d %d\n", res));
	}

#ifdef AP_ONLY
		ap_ssid.SSID_len = strlen(ap->ssid);
		strncpy(ap_ssid.SSID, ap->ssid, ap_ssid.SSID_len);
		res |= dev_wlc_ioctl(dev, WLC_SET_SSID, &ap_ssid, sizeof(ap_ssid));
		mpc = 0;
		res |= dev_wlc_intvar_set(dev, "mpc", mpc);
		if (strnicmp(ap->sec, "wep", strlen("wep")) == 0) {
			res |= dev_wlc_ioctl(dev, WLC_SET_KEY, &key, sizeof(key));
		}
#endif
	return res;
}


/*
*  param type : 0 array of char or u8, =1 param is integer;
* *str_ptr must already point to the beginning of the TOKEN
*/
static int
get_parameter_from_string(
			char **str_ptr, const char *token,
			int param_type, void  *dst, int param_max_len)
{
	char int_str[7] = "0";
	int parm_str_len;
	char  *param_str_begin;
	char  *param_str_end;
	char  *orig_str = *str_ptr;

	if ((*str_ptr) && !strncmp(*str_ptr, token, strlen(token))) {

		strsep(str_ptr, "=,"); /* find the 1st delimiter */
		param_str_begin = *str_ptr;
		strsep(str_ptr, "=,"); /* find the 2nd delimiter */

		if (*str_ptr == NULL) {
			/* corner case, last parameter in the string  */
			parm_str_len = strlen(param_str_begin);
		} else {
			param_str_end = *str_ptr-1;  /* pointer is set after the delim  */
			parm_str_len = param_str_end - param_str_begin;
		}

		WL_TRACE((" 'token:%s', len:%d, ", token, parm_str_len));

		if (parm_str_len > param_max_len) {
			WL_ERROR((" WARNING: extracted param len:%d is > MAX:%d\n",
				parm_str_len, param_max_len));

			parm_str_len = param_max_len;
		}

		switch (param_type) {

			case PTYPE_INTDEC: {
			/* string to decimal integer  */
				int *pdst_int = dst;
				char *eptr;

				if (parm_str_len > sizeof(int_str))
					 parm_str_len = sizeof(int_str);

				memcpy(int_str, param_str_begin, parm_str_len);

				*pdst_int = simple_strtoul(int_str, &eptr, 10);

				WL_TRACE((" written as integer:%d\n",  *pdst_int));
			}
			break;
			case PTYPE_STR_HEX: {
				u8 *buf = dst;
				/* ASI hex string to buffer  */
				param_max_len = param_max_len >> 1;  /* num of bytes */
				hstr_2_buf(param_str_begin, buf, param_max_len);
				dhd_print_buf(buf, param_max_len, 0);
			}
			break;
			default:
				/* param is array of ASCII chars, no convertion needed */
				memcpy(dst, param_str_begin, parm_str_len);
				*((char *)dst + parm_str_len) = 0; /* Z term */
				WL_ERROR((" written as a string:%s\n", (char *)dst));
			break;

		}

		return 0;
	} else {
		WL_ERROR(("\n %s: ERROR: can't find token:%s in str:%s \n",
			__FUNCTION__, token, orig_str));

	 return -1;
	}
}

/*
 *   ====== deassociate/deauthenticate SOFTAP stations ======
 */
static int
wl_iw_softap_deassoc_stations(struct net_device *dev)
{
	int i;
	int res = 0;
	char mac_buf[128] = {0};
	struct maclist *assoc_maclist = (struct maclist *) mac_buf;

	memset(assoc_maclist, 0, sizeof(mac_buf));
	assoc_maclist->count = 8; /* up 2  to 8 */

	res = dev_wlc_ioctl(dev, WLC_GET_ASSOCLIST, assoc_maclist, 128);
	if (res != 0) {
		WL_SOFTAP((" Error:%d in :%s, Couldn't get ASSOC List\n", res, __FUNCTION__));
		return res;
	}

	if (assoc_maclist->count)
		for (i = 0; i < assoc_maclist->count; i++) {
			scb_val_t scbval;
			scbval.val = htod32(1);
			/* copy src -> dst, len */
			bcopy(&assoc_maclist->ea[i], &scbval.ea, ETHER_ADDR_LEN);

			WL_SOFTAP(("deauth STA:%d \n", i));
			res |= dev_wlc_ioctl(dev, WLC_SCB_DEAUTHENTICATE_FOR_REASON,
			                     &scbval, sizeof(scb_val_t));

		}
	else
		WL_SOFTAP((" STA ASSOC list is empty\n"));

	if (res != 0)
		WL_SOFTAP((" Error:%d in :%s\n", res, __FUNCTION__));
	else if (assoc_maclist->count) {
		/* Extra delay is needed to wait till AP sends all Disassoc to the air */
		bcm_mdelay(200);
	}

	return res;
}


/*
 *  stop softap
 *  called with iwpriv AP_BSS_STOP
 *  going to send Disassoc to all stas and del wl0.1 interface
 *  Returns 0 of OK
 */
static int
iwpriv_softap_stop(struct net_device *dev,
	struct iw_request_info *info,
	union iwreq_data *wrqu,
	char *ext)
{
	int res = 0;

	WL_SOFTAP(("got iwpriv AP_BSS_STOP \n"));

	if ((!dev) && (!ap_net_dev)) {
		WL_ERROR(("%s: dev is null\n", __FUNCTION__));
		return res;
	}

	net_os_wake_lock(dev);
	DHD_OS_MUTEX_LOCK(&wl_softap_lock);

	if  ((ap_cfg_running == TRUE)) {
#ifdef AP_ONLY
		wl_iw_softap_deassoc_stations(dev);
#else
		wl_iw_softap_deassoc_stations(ap_net_dev);

		if ((res = dev_iw_write_cfg1_bss_var(dev, 2)) < 0)
			WL_ERROR(("%s failed to del BSS err = %d", __FUNCTION__, res));
#endif

		/* Delay is needed to let Dongle generate Event to the Host */
		bcm_mdelay(100);

		wrqu->data.length = 0;
		ap_cfg_running = FALSE;
	} else
		WL_ERROR(("%s: was called when SoftAP is OFF : move on\n", __FUNCTION__));

	WL_SOFTAP(("%s Done with %d\n", __FUNCTION__, res));
	DHD_OS_MUTEX_UNLOCK(&wl_softap_lock);
	net_os_wake_unlock(dev);

	return res;
}

/*
 * IWPRIV handler for testing WPA supplicant event/private commands
 * send a private event to WPA supplicant, wpa supp should relay
 * it back through the *IW EXT standard driver call: wl_iw_set_priv()
 */
/*
 *  =========  prep for a new FW download  ========
 */
static int
iwpriv_fw_reload(struct net_device *dev,
	struct iw_request_info *info,
	union iwreq_data *wrqu,
	char *ext)
{
	int ret = -1;
	char extra[256];
	char *fwstr = fw_path ; /* points to current Firmware path string */

	WL_SOFTAP(("current firmware_path[]=%s\n", fwstr));

	WL_TRACE((">Got FW_RELOAD cmd:"
	          "info->cmd:%x, info->flags:%x, u.data:%p, u.len:%d, "
	          "fw_path:%p, len:%d \n",
	          info->cmd, info->flags,
	          wrqu->data.pointer, wrqu->data.length, fwstr, strlen(fwstr)));

	if ((wrqu->data.length > 4) && (wrqu->data.length < sizeof(extra))) {
		char *str_ptr;

		if (copy_from_user(extra, wrqu->data.pointer, wrqu->data.length)) {
			ret = -EFAULT;
			goto exit_proc;
		}

		/* indicate which mode we are currently in "AP" or STA" */
		extra[wrqu->data.length] = 8;
		str_ptr = extra;

		if (get_parameter_from_string(&str_ptr,
		                              "FW_PATH=", PTYPE_STRING, fwstr, 255) != 0) {
			WL_ERROR(("Error: extracting FW_PATH='' string\n"));
			goto exit_proc;
		}

		if  (strstr(fwstr, "apsta") != NULL) {
			  WL_SOFTAP(("GOT APSTA FIRMWARE\n"));
			  ap_fw_loaded = TRUE;
		} else {
			WL_SOFTAP(("GOT STA FIRMWARE\n"));
			ap_fw_loaded = FALSE;
		}

		WL_SOFTAP(("SET firmware_path[]=%s , str_p:%p\n", fwstr, fwstr));
		ret = 0;
	} else {
		WL_ERROR(("Error: ivalid param len:%d\n", wrqu->data.length));
	}

exit_proc:
	return ret;
}

#ifdef SOFTAP
/* loop back test function only for debugging */
static int
iwpriv_wpasupp_loop_tst(struct net_device *dev,
            struct iw_request_info *info,
            union iwreq_data *wrqu,
            char *ext)
{
	int res = 0;
	char *params = NULL;

	WL_TRACE((">Got IWPRIV  wp_supp loopback cmd test:"
	          "info->cmd:%x, info->flags:%x, u.data:%p, u.len:%d\n",
	          info->cmd, info->flags,
	          wrqu->data.pointer, wrqu->data.length));

	if (wrqu->data.length != 0) {

		if (!(params = kmalloc(wrqu->data.length+1, GFP_KERNEL)))
			return -ENOMEM;


		if (copy_from_user(params, wrqu->data.pointer, wrqu->data.length)) {
			kfree(params);
			return -EFAULT;
		}

		params[wrqu->data.length] = 0;
		WL_SOFTAP(("\n>> copied from user:\n %s\n", params));
	} else {
		WL_ERROR(("ERROR param length is 0\n"));
		return -EFAULT;
	}

	/*  relay it to WPA supplicant */
	res = wl_iw_send_priv_event(dev, params);
	kfree(params);

	return res;
}
#endif /* SOFTAP */

/*
 * Called by iwpriv AP_BSS_START
 * Sets AP security from prepared my_ap config
 * and turn on wl0.1 interface
 * Returns zero if OK
 */
static int
iwpriv_en_ap_bss(
	struct net_device *dev,
	struct iw_request_info *info,
	void *wrqu,
	char *extra)
{
	int res = 0;

	if (!dev) {
		WL_ERROR(("%s: dev is null\n", __FUNCTION__));
		return -1;
	}

	net_os_wake_lock(dev);
	DHD_OS_MUTEX_LOCK(&wl_softap_lock);

	WL_TRACE(("%s: rcvd IWPRIV IOCTL:  for dev:%s\n", __FUNCTION__, dev->name));

	/* set all ap security from global val  */
#ifndef AP_ONLY
	if ((res = wl_iw_set_ap_security(dev, &my_ap)) != 0) {
		WL_ERROR((" %s ERROR setting SOFTAP security in :%d\n", __FUNCTION__, res));
	}
	else {
		/* kick off SoftAP BSS is UP */
		if ((res = dev_iw_write_cfg1_bss_var(dev, 1)) < 0)
			WL_ERROR(("%s fail to set bss up err=%d\n", __FUNCTION__, res));
		else
			/* Delay is needed to let Dongle generate Event to the Host */
			bcm_mdelay(100);
	}

#endif /* AP_ONLY */
	WL_SOFTAP(("%s done with res %d \n", __FUNCTION__, res));

	DHD_OS_MUTEX_UNLOCK(&wl_softap_lock);
	net_os_wake_unlock(dev);

	return res;
}

static int
get_assoc_sta_list(struct net_device *dev, char *buf, int len)
{
	/*	struct maclist *maclist = (struct maclist *) buf; */
	WL_TRACE(("calling dev_wlc_ioctl(dev:%p, cmd:%d, buf:%p, len:%d)\n",
		dev, WLC_GET_ASSOCLIST, buf, len));

	dev_wlc_ioctl(dev, WLC_GET_ASSOCLIST, buf, len);

	return 0;
}

/*
* ********* configure WL mac filtering function ********
*/
static int
set_ap_mac_list(struct net_device *dev, char *buf)
{
	struct mac_list_set *mac_list_set = (struct mac_list_set *)buf;
	struct maclist *white_maclist = (struct maclist *)&mac_list_set->white_list;
	struct maclist *black_maclist = (struct maclist *)&mac_list_set->black_list;
	int mac_mode = mac_list_set->mode;
	int length;
	int i;

	ap_macmode = mac_mode;
	if (mac_mode == MACLIST_MODE_DISABLED) {
		/* clear the black list */
		bzero(&ap_black_list, sizeof(struct mflist));

		/* set mac_mode to firmware */
		dev_wlc_ioctl(dev, WLC_SET_MACMODE, &mac_mode, sizeof(mac_mode));
	} else {
		scb_val_t scbval;
		char mac_buf[256] = {0};
		struct maclist *assoc_maclist = (struct maclist *) mac_buf;

		mac_mode = MACLIST_MODE_ALLOW;
		/* set mac_mode to firmware */
		dev_wlc_ioctl(dev, WLC_SET_MACMODE, &mac_mode, sizeof(mac_mode));

		/* set the white list */
		length = sizeof(white_maclist->count)+white_maclist->count*ETHER_ADDR_LEN;
		dev_wlc_ioctl(dev, WLC_SET_MACLIST, white_maclist, length);
		WL_SOFTAP(("White List, length %d:\n", length));
		for (i = 0; i < white_maclist->count; i++)
			WL_SOFTAP(("mac %d: %02X:%02X:%02X:%02X:%02X:%02X\n",
				i, white_maclist->ea[i].octet[0], white_maclist->ea[i].octet[1],
				white_maclist->ea[i].octet[2],
				white_maclist->ea[i].octet[3], white_maclist->ea[i].octet[4],
				white_maclist->ea[i].octet[5]));

		/* set the black list */
		bcopy(black_maclist, &ap_black_list, sizeof(ap_black_list));

		WL_SOFTAP(("Black List, size %d:\n", (int)sizeof(ap_black_list)));
		for (i = 0; i < ap_black_list.count; i++)
			WL_SOFTAP(("mac %d: %02X:%02X:%02X:%02X:%02X:%02X\n",
				i, ap_black_list.ea[i].octet[0], ap_black_list.ea[i].octet[1],
				ap_black_list.ea[i].octet[2],
				ap_black_list.ea[i].octet[3],
				ap_black_list.ea[i].octet[4], ap_black_list.ea[i].octet[5]));

		/* deauth if there is associated station not in list */
		dev_wlc_ioctl(dev, WLC_GET_ASSOCLIST, assoc_maclist, 256);
		if (assoc_maclist->count) {
			int j;
			for (i = 0; i < assoc_maclist->count; i++) {
				for (j = 0; j < white_maclist->count; j++) {
					if (!bcmp(&assoc_maclist->ea[i], &white_maclist->ea[j],
						ETHER_ADDR_LEN)) {
						WL_SOFTAP(("match allow, let it be\n"));
						break;
					}
				}
				if (j == white_maclist->count) {
						WL_SOFTAP(("match black, deauth it\n"));
						scbval.val = htod32(1);
						bcopy(&assoc_maclist->ea[i], &scbval.ea,
						ETHER_ADDR_LEN);
						dev_wlc_ioctl(dev,
							WLC_SCB_DEAUTHENTICATE_FOR_REASON, &scbval,
							sizeof(scb_val_t));
				}
			}
		}
	}
	return 0;
}
#endif /* SOFTAP */


/*
 * --- Process Android WPA supplicant commands sent as ASCII strings ----
 */
#ifdef SOFTAP
#define PARAM_OFFSET PROFILE_OFFSET

static int
wl_iw_process_private_ascii_cmd(
			struct net_device *dev,
			struct iw_request_info *info,
			union iwreq_data *dwrq,
			char *cmd_str)
{
	int ret = 0;
	char *sub_cmd = cmd_str + PROFILE_OFFSET + strlen("ASCII_CMD=");

	WL_SOFTAP(("\n %s: ASCII_CMD: offs_0:%s, offset_32:\n'%s'\n",
		__FUNCTION__, cmd_str, cmd_str + PROFILE_OFFSET));

	if (strnicmp(sub_cmd, "AP_CFG", strlen("AP_CFG")) == 0) {

		WL_SOFTAP((" AP_CFG \n"));

		/*  note the actual cmd/param string is at offset 32  */
		if (init_ap_profile_from_string(cmd_str+PROFILE_OFFSET, &my_ap) != 0) {
				WL_ERROR(("ERROR: SoftAP CFG prams !\n"));
				ret = -1;
		} else {
			ret = set_ap_cfg(dev, &my_ap);
		}

	} else if (strnicmp(sub_cmd, "AP_BSS_START", strlen("AP_BSS_START")) == 0) {

		WL_SOFTAP(("\n SOFTAP - ENABLE BSS \n"));

		/* make sure that wl0.1 SOFTAP net device interface is created   */
		WL_SOFTAP(("\n!!! got 'WL_AP_EN_BSS' from WPA supplicant, dev:%s\n", dev->name));

#ifndef AP_ONLY
		if (ap_net_dev == NULL) {
				 printf("\n ERROR: SOFTAP net_dev* is NULL !!!\n");
		} else {
			  /* START SOFT AP */
			if ((ret = iwpriv_en_ap_bss(ap_net_dev, info, dwrq, cmd_str)) < 0)
				WL_ERROR(("%s line %d fail to set bss up\n",
					__FUNCTION__, __LINE__));
		}
#else
		if ((ret = iwpriv_en_ap_bss(dev, info, dwrq, cmd_str)) < 0)
				WL_ERROR(("%s line %d fail to set bss up\n",
					__FUNCTION__, __LINE__));
#endif
	} else if (strnicmp(sub_cmd, "ASSOC_LST", strlen("ASSOC_LST")) == 0) {

		/* TODO call iwpriv get assoc list handler  */

	} else if (strnicmp(sub_cmd, "AP_BSS_STOP", strlen("AP_BSS_STOP")) == 0) {

		WL_SOFTAP((" \n temp DOWN SOFTAP\n"));
#ifndef AP_ONLY
		if ((ret = dev_iw_write_cfg1_bss_var(dev, 0)) < 0) {
				WL_ERROR(("%s line %d fail to set bss down\n",
					__FUNCTION__, __LINE__));
		}
#endif
	}

	return ret;

}
#endif /* SOFTAP */

static int
wl_iw_set_priv(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_point *dwrq,
	char *ext
)
{
	int ret = 0;
	char * extra;

	if (!(extra = kmalloc(dwrq->length, GFP_KERNEL)))
	    return -ENOMEM;

	if (copy_from_user(extra, dwrq->pointer, dwrq->length)) {
	    kfree(extra);
	    return -EFAULT;
	}

	WL_TRACE(("%s: SIOCSIWPRIV request %s, info->cmd:%x, info->flags:%d\n dwrq->length:%d\n",
		dev->name, extra, info->cmd, info->flags, dwrq->length));

	/*
	 * Adding support to Android UI private IOTCL
	*/

	net_os_wake_lock(dev);

	if (dwrq->length && extra) {
		/* Lin - a clear logic, on if it is a "START", then check the state */
		if (strnicmp(extra, "START", strlen("START")) == 0) {
			wl_iw_control_wl_on(dev, info);
			WL_TRACE(("%s, Received regular START command\n", __FUNCTION__));
		}

		if (g_onoff == G_WLAN_SET_OFF) {
			WL_TRACE(("%s, missing START, Fail\n", __FUNCTION__));
			kfree(extra);
			net_os_wake_unlock(dev);
			return -EFAULT;
		}

	    if (strnicmp(extra, "SCAN-ACTIVE", strlen("SCAN-ACTIVE")) == 0) {
#ifdef ENABLE_ACTIVE_PASSIVE_SCAN_SUPPRESS
			WL_TRACE(("%s: active scan setting suppressed\n", dev->name));
#else
			ret = wl_iw_set_active_scan(dev, info, (union iwreq_data *)dwrq, extra);
#endif /* PASSIVE_SCAN_SUPPRESS */
	    }
	    else if (strnicmp(extra, "SCAN-PASSIVE", strlen("SCAN-PASSIVE")) == 0)
#ifdef ENABLE_ACTIVE_PASSIVE_SCAN_SUPPRESS
			WL_TRACE(("%s: passive scan setting suppressed\n", dev->name));
#else
			ret = wl_iw_set_passive_scan(dev, info, (union iwreq_data *)dwrq, extra);
#endif /* PASSIVE_SCAN_SUPPRESS */
	    else if (strnicmp(extra, "RSSI", strlen("RSSI")) == 0)
			ret = wl_iw_get_rssi(dev, info, (union iwreq_data *)dwrq, extra);
	    else if (strnicmp(extra, "LINKSPEED", strlen("LINKSPEED")) == 0)
			ret = wl_iw_get_link_speed(dev, info, (union iwreq_data *)dwrq, extra);
	    else if (strnicmp(extra, "MACADDR", strlen("MACADDR")) == 0)
			ret = wl_iw_get_macaddr(dev, info, (union iwreq_data *)dwrq, extra);
	    else if (strnicmp(extra, "COUNTRY", strlen("COUNTRY")) == 0)
			ret = wl_iw_set_country(dev, info, (union iwreq_data *)dwrq, extra);
	    else if (strnicmp(extra, "STOP", strlen("STOP")) == 0)
			ret = wl_iw_control_wl_off(dev, info);
	    else if (strnicmp(extra, BAND_GET_CMD, strlen(BAND_GET_CMD)) == 0)
			ret = wl_iw_get_band(dev, info, (union iwreq_data *)dwrq, extra);
	    else if (strnicmp(extra, BAND_SET_CMD, strlen(BAND_SET_CMD)) == 0)
			ret = wl_iw_set_band(dev, info, (union iwreq_data *)dwrq, extra);
	    else if (strnicmp(extra, DTIM_SKIP_GET_CMD, strlen(DTIM_SKIP_GET_CMD)) == 0)
			ret = wl_iw_get_dtim_skip(dev, info, (union iwreq_data *)dwrq, extra);
	    else if (strnicmp(extra, DTIM_SKIP_SET_CMD, strlen(DTIM_SKIP_SET_CMD)) == 0)
			ret = wl_iw_set_dtim_skip(dev, info, (union iwreq_data *)dwrq, extra);
	    else if (strnicmp(extra, SETSUSPEND_CMD, strlen(SETSUSPEND_CMD)) == 0)
			ret = wl_iw_set_suspend(dev, info, (union iwreq_data *)dwrq, extra);
#if defined(PNO_SUPPORT)
	    else if (strnicmp(extra, PNOSSIDCLR_SET_CMD, strlen(PNOSSIDCLR_SET_CMD)) == 0)
			ret = wl_iw_set_pno_reset(dev, info, (union iwreq_data *)dwrq, extra);
	    else if (strnicmp(extra, PNOSETUP_SET_CMD, strlen(PNOSETUP_SET_CMD)) == 0)
			ret = wl_iw_set_pno_set(dev, info, (union iwreq_data *)dwrq, extra);
	    else if (strnicmp(extra, PNOENABLE_SET_CMD, strlen(PNOENABLE_SET_CMD)) == 0)
			ret = wl_iw_set_pno_enable(dev, info, (union iwreq_data *)dwrq, extra);
#endif /* PNO_SUPPORT */
#if defined(CSCAN)
	    /* wpa_supplicant should support ComboSCAN request as well */
	    else if (strnicmp(extra, CSCAN_COMMAND, strlen(CSCAN_COMMAND)) == 0)
			ret = wl_iw_set_cscan(dev, info, (union iwreq_data *)dwrq, extra);
#endif /* (OEM_ANDROID) && defined(CSCAN) */
#ifdef CONFIG_MACH_MAHIMAHI
	    else if (strnicmp(extra, "POWERMODE", strlen("POWERMODE")) == 0)
			ret = wl_iw_set_power_mode(dev, info, (union iwreq_data *)dwrq, extra);
	    else if (strnicmp(extra, "BTCOEXMODE", strlen("BTCOEXMODE")) == 0)
			ret = wl_iw_set_btcoex_dhcp(dev, info, (union iwreq_data *)dwrq, extra);
	    else if (strnicmp(extra, "GETPOWER", strlen("GETPOWER")) == 0)
			ret = wl_iw_get_power_mode(dev, info, (union iwreq_data *)dwrq, extra);
#else
	    else if (strnicmp(extra, "POWERMODE", strlen("POWERMODE")) == 0)
			ret = wl_iw_set_btcoex_dhcp(dev, info, (union iwreq_data *)dwrq, extra);
#endif
#ifdef SOFTAP
	    else if (strnicmp(extra, "ASCII_CMD", strlen("ASCII_CMD")) == 0) {
	        /* process android WPA supplicant commands sent as an ASCII strings  */
		    wl_iw_process_private_ascii_cmd(dev, info, (union iwreq_data *)dwrq, extra);
	    }
		else if (strnicmp(extra, "AP_MAC_LIST_SET", strlen("AP_MAC_LIST_SET")) == 0) {
			WL_SOFTAP(("penguin, set AP_MAC_LIST_SET\n"));
			set_ap_mac_list(dev, (extra + PROFILE_OFFSET));
	    }
#endif /* SOFTAP */
	    else {
			WL_TRACE(("Unknown PRIVATE command %s\n", extra));
			snprintf(extra, MAX_WX_STRING, "OK");
			dwrq->length = strlen("OK") + 1;
			WL_ERROR(("Unknown PRIVATE command, ignored\n"));
		}
	}

	net_os_wake_unlock(dev);

	if (extra) {
	    if (copy_to_user(dwrq->pointer, extra, dwrq->length)) {
			kfree(extra);
			return -EFAULT;
	    }

	    kfree(extra);
	}

	return ret;
}

static const iw_handler wl_iw_handler[] =
{
	(iw_handler) wl_iw_config_commit,	/* SIOCSIWCOMMIT */
	(iw_handler) wl_iw_get_name,		/* SIOCGIWNAME */
	(iw_handler) NULL,			/* SIOCSIWNWID */
	(iw_handler) NULL,			/* SIOCGIWNWID */
	(iw_handler) wl_iw_set_freq,		/* SIOCSIWFREQ */
	(iw_handler) wl_iw_get_freq,		/* SIOCGIWFREQ */
	(iw_handler) wl_iw_set_mode,		/* SIOCSIWMODE */
	(iw_handler) wl_iw_get_mode,		/* SIOCGIWMODE */
	(iw_handler) NULL,			/* SIOCSIWSENS */
	(iw_handler) NULL,			/* SIOCGIWSENS */
	(iw_handler) NULL,			/* SIOCSIWRANGE */
	(iw_handler) wl_iw_get_range,		/* SIOCGIWRANGE */
	(iw_handler) wl_iw_set_priv,		/* SIOCSIWPRIV */
	(iw_handler) NULL,			/* SIOCGIWPRIV */
	(iw_handler) NULL,			/* SIOCSIWSTATS */
	(iw_handler) NULL,			/* SIOCGIWSTATS */
	(iw_handler) wl_iw_set_spy,		/* SIOCSIWSPY */
	(iw_handler) wl_iw_get_spy,		/* SIOCGIWSPY */
	(iw_handler) NULL,			/* -- hole -- */
	(iw_handler) NULL,			/* -- hole -- */
	(iw_handler) wl_iw_set_wap,		/* SIOCSIWAP */
	(iw_handler) wl_iw_get_wap,		/* SIOCGIWAP */
#if WIRELESS_EXT > 17
	(iw_handler) wl_iw_mlme,		/* SIOCSIWMLME */
#else
	(iw_handler) NULL,			/* -- hole -- */
#endif
#if defined(WL_IW_USE_ISCAN)
	(iw_handler) wl_iw_iscan_get_aplist,	/* SIOCGIWAPLIST */
#else
	(iw_handler) wl_iw_get_aplist,		/* SIOCGIWAPLIST */
#endif 
#if WIRELESS_EXT > 13
#if defined(WL_IW_USE_ISCAN)
	(iw_handler) wl_iw_iscan_set_scan,	/* SIOCSIWSCAN */
	(iw_handler) wl_iw_iscan_get_scan,	/* SIOCGIWSCAN */
#else
	(iw_handler) wl_iw_set_scan,		/* SIOCSIWSCAN */
	(iw_handler) wl_iw_get_scan,		/* SIOCGIWSCAN */
#endif
#else	/* WIRELESS_EXT > 13 */
	(iw_handler) NULL,			/* SIOCSIWSCAN */
	(iw_handler) NULL,			/* SIOCGIWSCAN */
#endif	/* WIRELESS_EXT > 13 */
	(iw_handler) wl_iw_set_essid,		/* SIOCSIWESSID */
	(iw_handler) wl_iw_get_essid,		/* SIOCGIWESSID */
	(iw_handler) wl_iw_set_nick,		/* SIOCSIWNICKN */
	(iw_handler) wl_iw_get_nick,		/* SIOCGIWNICKN */
	(iw_handler) NULL,			/* -- hole -- */
	(iw_handler) NULL,			/* -- hole -- */
	(iw_handler) wl_iw_set_rate,		/* SIOCSIWRATE */
	(iw_handler) wl_iw_get_rate,		/* SIOCGIWRATE */
	(iw_handler) wl_iw_set_rts,		/* SIOCSIWRTS */
	(iw_handler) wl_iw_get_rts,		/* SIOCGIWRTS */
	(iw_handler) wl_iw_set_frag,		/* SIOCSIWFRAG */
	(iw_handler) wl_iw_get_frag,		/* SIOCGIWFRAG */
	(iw_handler) wl_iw_set_txpow,		/* SIOCSIWTXPOW */
	(iw_handler) wl_iw_get_txpow,		/* SIOCGIWTXPOW */
#if WIRELESS_EXT > 10
	(iw_handler) wl_iw_set_retry,		/* SIOCSIWRETRY */
	(iw_handler) wl_iw_get_retry,		/* SIOCGIWRETRY */
#endif /* WIRELESS_EXT > 10 */
	(iw_handler) wl_iw_set_encode,		/* SIOCSIWENCODE */
	(iw_handler) wl_iw_get_encode,		/* SIOCGIWENCODE */
	(iw_handler) wl_iw_set_power,		/* SIOCSIWPOWER */
	(iw_handler) wl_iw_get_power,		/* SIOCGIWPOWER */
#if WIRELESS_EXT > 17
	(iw_handler) NULL,			/* -- hole -- */
	(iw_handler) NULL,			/* -- hole -- */
	(iw_handler) wl_iw_set_wpaie,		/* SIOCSIWGENIE */
	(iw_handler) wl_iw_get_wpaie,		/* SIOCGIWGENIE */
	(iw_handler) wl_iw_set_wpaauth,		/* SIOCSIWAUTH */
	(iw_handler) wl_iw_get_wpaauth,		/* SIOCGIWAUTH */
	(iw_handler) wl_iw_set_encodeext,	/* SIOCSIWENCODEEXT */
	(iw_handler) wl_iw_get_encodeext,	/* SIOCGIWENCODEEXT */
	(iw_handler) wl_iw_set_pmksa,			/* SIOCSIWPMKSA */
#endif /* WIRELESS_EXT > 17 */
};

#if WIRELESS_EXT > 12
static const iw_handler wl_iw_priv_handler[] = {
	NULL,
	(iw_handler)wl_iw_set_active_scan,
	NULL,
	(iw_handler)wl_iw_get_rssi,
	NULL,
	(iw_handler)wl_iw_set_passive_scan,
	NULL,
	(iw_handler)wl_iw_get_link_speed,
	NULL,
	(iw_handler)wl_iw_get_macaddr,
	NULL,
	(iw_handler)wl_iw_control_wl_off,
	NULL,
	(iw_handler)wl_iw_control_wl_on,
#ifdef SOFTAP     /*  export SOFT_AP private commands ** */

	/* AP_SET_CFG configure AP from IWPRIV CMD LINE  */
	NULL,
	(iw_handler)iwpriv_set_ap_config,

	/* get list of currently associated stations  */
	/* NOTE: to see the list use dmesg */
	NULL,
	(iw_handler)iwpriv_get_assoc_list,

	/* SET MAC filters WHITE & BLACKLIST */
	NULL,
	(iw_handler)iwpriv_set_mac_filters,

	/* AP_BSS_START  final call to start SoftAP */
	NULL,
	(iw_handler)iwpriv_en_ap_bss,

	/* Xperemental loop_back test handler: receives an ASCII string argument
	and forwards it to wpa_supplicant as an event. WPA supplicant will send
	it back as if it was it's own command to the driver
	NOTE: wpa supplicant's 'driver_wext.c' needs to handle LPB_CMD_EVENT
	from the driver and iimplement. driver_cmd function on struct wpa_driver_ops
	*/
	NULL,
	(iw_handler)iwpriv_wpasupp_loop_tst,
	/* AP_BSS_STOP Stop SOFTAP */
	NULL,
	(iw_handler)iwpriv_softap_stop,
	/* FW RELOAD  */
	NULL,
	(iw_handler)iwpriv_fw_reload,
#endif /* SOFTAP */
#if defined(CSCAN)
	/* Combined scan call */
	NULL,
	(iw_handler)iwpriv_set_cscan
#endif 	
};

static const struct iw_priv_args wl_iw_priv_args[] =
{
	{	/* iwpriv unique IOCTL number */
		WL_IW_SET_ACTIVE_SCAN,
		0,
		IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_FIXED | MAX_WX_STRING,
		"SCAN-ACTIVE"
	},
	{
		WL_IW_GET_RSSI,
		0,
		IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_FIXED | MAX_WX_STRING,
		"RSSI"
	},
	{
		WL_IW_SET_PASSIVE_SCAN,
		0,
		IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_FIXED | MAX_WX_STRING,
		"SCAN-PASSIVE"
	},
	{
		WL_IW_GET_LINK_SPEED,
		0,
		IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_FIXED | MAX_WX_STRING,
		"LINKSPEED"
	},
	{
		WL_IW_GET_CURR_MACADDR,
		0,
		IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_FIXED | MAX_WX_STRING,
		"Macaddr"
	},
	{
		WL_IW_SET_STOP,
		0,
		IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_FIXED | MAX_WX_STRING,
		"STOP"
	},
	{
		WL_IW_SET_START,
		0,
		IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_FIXED | MAX_WX_STRING,
		"START"
	},

#ifdef SOFTAP
	/*
	 * *****  IWPRIV SOFTAP argument descriptors  ******
	 */
	/*  configure accesspoint from IWPRIV cmd line  */
	{
		WL_SET_AP_CFG,
		IW_PRIV_TYPE_CHAR |  256,      /* string , variable size up to to 200 chars */
		0,
		"AP_SET_CFG"
	},

	{
		WL_AP_STA_LIST,
		0,                     /* no parameters for SET */
		IW_PRIV_TYPE_CHAR | 0, /*  get var size  */
		"AP_GET_STA_LIST"
	},

	{
		WL_AP_MAC_FLTR,
		IW_PRIV_TYPE_CHAR | 256,                      /*  set mac filter  */
		IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_FIXED | 0,    /* no get   */
		"AP_SET_MAC_FLTR"
	},

	{ /*  additional command to get things rolling in the AP land :) */
		WL_AP_BSS_START,
		0,
		IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_FIXED | MAX_WX_STRING,
		"AP_BSS_START"
	},

	{
		AP_LPB_CMD,
		IW_PRIV_TYPE_CHAR | 256,   /* set, long str cmd wpa supp loopback test   */
		IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_FIXED | 0,    /* fixed get   */
		"AP_LPB_CMD"
	},

	{ /*  STOP access point */
		WL_AP_STOP,
		IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_FIXED | 0,   /* set params */
		IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_FIXED | 0,   /* get params */
		"AP_BSS_STOP"
	},
	{ /*  set a new FW to be reloaded upon START */
		WL_FW_RELOAD,
		IW_PRIV_TYPE_CHAR | 256,
		IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_FIXED | 0,
		"WL_FW_RELOAD"
	},
#endif /* SOFTAP */
#if defined(CSCAN)
	{ /*  set a combined scan */
		WL_COMBO_SCAN,
		IW_PRIV_TYPE_CHAR | 1024,  /* string , variable size up to to 200 chars */
		0,
		"CSCAN"
	},
#endif 
	};

const struct iw_handler_def wl_iw_handler_def =
{
	.num_standard = ARRAYSIZE(wl_iw_handler),
	.standard = (iw_handler *) wl_iw_handler,
#ifdef CONFIG_WEXT_PRIV	
	.num_private = ARRAYSIZE(wl_iw_priv_handler),
	.num_private_args = ARRAY_SIZE(wl_iw_priv_args),
	.private = (iw_handler *)wl_iw_priv_handler,
	.private_args = (void *) wl_iw_priv_args,
#endif
#if WIRELESS_EXT >= 19
	get_wireless_stats: dhd_get_wireless_stats,
#endif /* WIRELESS_EXT >= 19 */
	};
#endif /* WIRELESS_EXT > 12 */


/*
 *  DHD driver IOCTLS in (cmd >= SIOCIWFIRST) && (cmd <= SIOCIWLAST)) range land in here
 *
 */
int
wl_iw_ioctl(
	struct net_device *dev,
	struct ifreq *rq,
	int cmd
)
{
	struct iwreq *wrq = (struct iwreq *) rq;
	struct iw_request_info info;
	iw_handler handler;
	char *extra = NULL;
	size_t token_size = 1;
	int max_tokens = 0, ret = 0;

	net_os_wake_lock(dev);

	WL_TRACE(("\n%s, cmd:%x called via dhd->do_ioctl()entry point\n", __FUNCTION__, cmd));
	if (cmd < SIOCIWFIRST ||
		IW_IOCTL_IDX(cmd) >= ARRAYSIZE(wl_iw_handler) ||
		!(handler = wl_iw_handler[IW_IOCTL_IDX(cmd)])) {
			WL_ERROR(("%s: error in cmd=%x : not supported\n", __FUNCTION__, cmd));
			net_os_wake_unlock(dev);
			return -EOPNOTSUPP;
	}

	switch (cmd) {

	case SIOCSIWESSID:
	case SIOCGIWESSID:
	case SIOCSIWNICKN:
	case SIOCGIWNICKN:
		max_tokens = IW_ESSID_MAX_SIZE + 1;
		break;

	case SIOCSIWENCODE:
	case SIOCGIWENCODE:
#if WIRELESS_EXT > 17
	case SIOCSIWENCODEEXT:
	case SIOCGIWENCODEEXT:
#endif
		max_tokens = wrq->u.data.length;
		break;

	case SIOCGIWRANGE:
		/*  GG Adding stange 500 bytes to match the same extra number of bytes
			from wpa_supplicant code, function wpa_driver_wext_get_range
			From 2.6.31 it will be called directly and not from the handle so
			we have to make adjustment to pass checking below
		*/
		max_tokens = sizeof(struct iw_range) + 500;
		break;

	case SIOCGIWAPLIST:
		token_size = sizeof(struct sockaddr) + sizeof(struct iw_quality);
		max_tokens = IW_MAX_AP;
		break;

#if WIRELESS_EXT > 13
	case SIOCGIWSCAN:
#if defined(WL_IW_USE_ISCAN)
	if (g_iscan)
		max_tokens = wrq->u.data.length;
	else
#endif
		max_tokens = IW_SCAN_MAX_DATA;
		break;
#endif /* WIRELESS_EXT > 13 */

	case SIOCSIWSPY:
		token_size = sizeof(struct sockaddr);
		max_tokens = IW_MAX_SPY;
		break;

	case SIOCGIWSPY:
		token_size = sizeof(struct sockaddr) + sizeof(struct iw_quality);
		max_tokens = IW_MAX_SPY;
		break;

#if WIRELESS_EXT > 17
	case SIOCSIWPMKSA:
	case SIOCSIWGENIE:
#endif /* WIRELESS_EXT > 17 */
	case SIOCSIWPRIV:
		max_tokens = wrq->u.data.length;
		break;
	}

	if (max_tokens && wrq->u.data.pointer) {
		if (wrq->u.data.length > max_tokens) {
			WL_ERROR(("%s: error in cmd=%x wrq->u.data.length=%d  > max_tokens=%d\n",
				__FUNCTION__, cmd, wrq->u.data.length, max_tokens));
			ret = -E2BIG;
			goto wl_iw_ioctl_done;
		}
		if (!(extra = kmalloc(max_tokens * token_size, GFP_KERNEL))) {
			ret = -ENOMEM;
			goto wl_iw_ioctl_done;
		}

		if (copy_from_user(extra, wrq->u.data.pointer, wrq->u.data.length * token_size)) {
			kfree(extra);
			ret = -EFAULT;
			goto wl_iw_ioctl_done;
		}
	}

	info.cmd = cmd;
	info.flags = 0;

	ret = handler(dev, &info, &wrq->u, extra);

	if (extra) {
		if (copy_to_user(wrq->u.data.pointer, extra, wrq->u.data.length * token_size)) {
			kfree(extra);
			ret = -EFAULT;
			goto wl_iw_ioctl_done;
		}

		kfree(extra);
	}

wl_iw_ioctl_done:

	net_os_wake_unlock(dev);

	return ret;
}

/* Convert a connection status event into a connection status string.
 * Returns TRUE if a matching connection status string was found.
 */
static bool
wl_iw_conn_status_str(uint32 event_type, uint32 status, uint32 reason,
	char* stringBuf, uint buflen)
{
	typedef struct conn_fail_event_map_t {
		uint32 inEvent;			/* input: event type to match */
		uint32 inStatus;		/* input: event status code to match */
		uint32 inReason;		/* input: event reason code to match */
		const char* outName;	/* output: failure type */
		const char* outCause;	/* output: failure cause */
	} conn_fail_event_map_t;

	/* Map of WLC_E events to connection failure strings */
#define WL_IW_DONT_CARE	9999
	const conn_fail_event_map_t event_map [] = {
		/* inEvent           inStatus                inReason         */
		/* outName outCause                                           */
		{WLC_E_SET_SSID,     WLC_E_STATUS_SUCCESS,   WL_IW_DONT_CARE,
		"Conn", "Success"},
		{WLC_E_SET_SSID,     WLC_E_STATUS_NO_NETWORKS, WL_IW_DONT_CARE,
		"Conn", "NoNetworks"},
		{WLC_E_SET_SSID,     WLC_E_STATUS_FAIL,      WL_IW_DONT_CARE,
		"Conn", "ConfigMismatch"},
		{WLC_E_PRUNE,        WL_IW_DONT_CARE,        WLC_E_PRUNE_ENCR_MISMATCH,
		"Conn", "EncrypMismatch"},
		{WLC_E_PRUNE,        WL_IW_DONT_CARE,        WLC_E_RSN_MISMATCH,
		"Conn", "RsnMismatch"},
		{WLC_E_AUTH,         WLC_E_STATUS_TIMEOUT,   WL_IW_DONT_CARE,
		"Conn", "AuthTimeout"},
		{WLC_E_AUTH,         WLC_E_STATUS_FAIL,      WL_IW_DONT_CARE,
		"Conn", "AuthFail"},
		{WLC_E_AUTH,         WLC_E_STATUS_NO_ACK,    WL_IW_DONT_CARE,
		"Conn", "AuthNoAck"},
		{WLC_E_REASSOC,      WLC_E_STATUS_FAIL,      WL_IW_DONT_CARE,
		"Conn", "ReassocFail"},
		{WLC_E_REASSOC,      WLC_E_STATUS_TIMEOUT,   WL_IW_DONT_CARE,
		"Conn", "ReassocTimeout"},
		{WLC_E_REASSOC,      WLC_E_STATUS_ABORT,     WL_IW_DONT_CARE,
		"Conn", "ReassocAbort"},
		{WLC_E_PSK_SUP,      WLC_SUP_KEYED,          WL_IW_DONT_CARE,
		"Sup", "ConnSuccess"},
		{WLC_E_PSK_SUP,      WL_IW_DONT_CARE,        WL_IW_DONT_CARE,
		"Sup", "WpaHandshakeFail"},
		{WLC_E_DEAUTH_IND,   WL_IW_DONT_CARE,        WL_IW_DONT_CARE,
		"Conn", "Deauth"},
		{WLC_E_DISASSOC_IND, WL_IW_DONT_CARE,        WL_IW_DONT_CARE,
		"Conn", "DisassocInd"},
		{WLC_E_DISASSOC,     WL_IW_DONT_CARE,        WL_IW_DONT_CARE,
		"Conn", "Disassoc"}
	};

	const char* name = "";
	const char* cause = NULL;
	int i;

	/* Search the event map table for a matching event */
	for (i = 0;  i < sizeof(event_map)/sizeof(event_map[0]);  i++) {
		const conn_fail_event_map_t* row = &event_map[i];
		if (row->inEvent == event_type &&
		    (row->inStatus == status || row->inStatus == WL_IW_DONT_CARE) &&
		    (row->inReason == reason || row->inReason == WL_IW_DONT_CARE)) {
			name = row->outName;
			cause = row->outCause;
			break;
		}
	}

	/* If found, generate a connection failure string and return TRUE */
	if (cause) {
		memset(stringBuf, 0, buflen);
		snprintf(stringBuf, buflen, "%s %s %02d %02d",
			name, cause, status, reason);
		WL_INFORM(("Connection status: %s\n", stringBuf));
		return TRUE;
	} else {
		return FALSE;
	}
}

#if WIRELESS_EXT > 14
/* Check if we have received an event that indicates connection failure
 * If so, generate a connection failure report string.
 * The caller supplies a buffer to hold the generated string.
 */
static bool
wl_iw_check_conn_fail(wl_event_msg_t *e, char* stringBuf, uint buflen)
{
	uint32 event = ntoh32(e->event_type);
	uint32 status =  ntoh32(e->status);
	uint32 reason =  ntoh32(e->reason);

	if (wl_iw_conn_status_str(event, status, reason, stringBuf, buflen)) {
		return TRUE;
	}
	else
		return FALSE;
}
#endif /* WIRELESS_EXT > 14 */

#ifndef IW_CUSTOM_MAX
#define IW_CUSTOM_MAX 256 /* size of extra buffer used for translation of events */
#endif /* IW_CUSTOM_MAX */

void
wl_iw_event(struct net_device *dev, wl_event_msg_t *e, void* data)
{
#if WIRELESS_EXT > 13
	union iwreq_data wrqu;
	char extra[IW_CUSTOM_MAX + 1];
	int cmd = 0;
	uint32 event_type = ntoh32(e->event_type);
	uint16 flags =  ntoh16(e->flags);
	uint32 datalen = ntoh32(e->datalen);
	uint32 status =  ntoh32(e->status);
	uint32 toto;
	static  uint32 roam_no_success = 0;
	static bool roam_no_success_send = FALSE;
	memset(&wrqu, 0, sizeof(wrqu));
	memset(extra, 0, sizeof(extra));

	if (!dev) {
		WL_ERROR(("%s: dev is null\n", __FUNCTION__));
		return;
	}

	net_os_wake_lock(dev);

	WL_TRACE(("%s: dev=%s event=%d \n", __FUNCTION__, dev->name, event_type));

	/* Different Events may pointed to the different structures.
	  * See union (!) iwreq_data declaration for IOCTL REQUEST in WPA_SUPPLICANT code
	  * Refer to desciptions and header_type of each Event in WPA_SUPPLICAN code
	  * and your kernel wext.c file before adding any new case after switch
	  */
	switch (event_type) {
#if defined(SOFTAP)
	case WLC_E_PRUNE:
		if (ap_cfg_running) {
			char *macaddr = (char *)&e->addr;
			WL_SOFTAP(("PRUNE received, %02X:%02X:%02X:%02X:%02X:%02X!\n",
				macaddr[0], macaddr[1], macaddr[2], macaddr[3],
				macaddr[4], macaddr[5]));

			/* send the message if macmode enabled */
			if (ap_macmode)
			{
				int i;
				for (i = 0; i < ap_black_list.count; i++) {
					if (!bcmp(macaddr, &ap_black_list.ea[i],
						sizeof(struct ether_addr))) {
						WL_SOFTAP(("mac in black list, ignore it\n"));
						break;
					}
				}

				if (i == ap_black_list.count) {
					/* mac not found in black list, send notify */
					char mac_buf[32] = {0};
					sprintf(mac_buf, "STA_BLOCK %02X:%02X:%02X:%02X:%02X:%02X",
						macaddr[0], macaddr[1], macaddr[2],
						macaddr[3], macaddr[4], macaddr[5]);
					wl_iw_send_priv_event(priv_dev, mac_buf);
				}
			}
		}
		break;
#endif 
	case WLC_E_TXFAIL:
		cmd = IWEVTXDROP;
		memcpy(wrqu.addr.sa_data, &e->addr, ETHER_ADDR_LEN);
		wrqu.addr.sa_family = ARPHRD_ETHER;
		break;
#if WIRELESS_EXT > 14
	case WLC_E_JOIN:
	case WLC_E_ASSOC_IND:
	case WLC_E_REASSOC_IND:
#if defined(SOFTAP)
		WL_SOFTAP(("STA connect received %d\n", event_type));
		if (ap_cfg_running) {
			wl_iw_send_priv_event(priv_dev, "STA_JOIN");
			goto wl_iw_event_end;
		}
#endif 
		memcpy(wrqu.addr.sa_data, &e->addr, ETHER_ADDR_LEN);
		wrqu.addr.sa_family = ARPHRD_ETHER;
		cmd = IWEVREGISTERED;
		break;
	case WLC_E_ROAM:
		if (status != WLC_E_STATUS_SUCCESS) {
			roam_no_success++;
			if ((roam_no_success == 3) && (roam_no_success_send == FALSE)) {
				/* Inform Supplicant that link down: BSSID iz zero */
				roam_no_success_send = TRUE;
				bzero(wrqu.addr.sa_data, ETHER_ADDR_LEN);
				bzero(&extra, ETHER_ADDR_LEN);
				cmd = SIOCGIWAP;
				WL_ERROR(("%s  ROAMING did not succeeded , send Link Down\n",
					__FUNCTION__));
			} else {
				WL_TRACE(("##### ROAMING did not succeeded %d\n", roam_no_success));
				goto wl_iw_event_end;
			}
		} else {
			memcpy(wrqu.addr.sa_data, &e->addr.octet, ETHER_ADDR_LEN);
			wrqu.addr.sa_family = ARPHRD_ETHER;
			cmd = SIOCGIWAP;
		}
	break;
	case WLC_E_DEAUTH_IND:
	case WLC_E_DISASSOC_IND:
#if defined(SOFTAP)
		WL_SOFTAP(("STA disconnect received %d\n", event_type));
		if (ap_cfg_running) {
			wl_iw_send_priv_event(priv_dev, "STA_LEAVE");
			goto wl_iw_event_end;
		}
#endif 
		cmd = SIOCGIWAP;
		bzero(wrqu.addr.sa_data, ETHER_ADDR_LEN);
		wrqu.addr.sa_family = ARPHRD_ETHER;
		bzero(&extra, ETHER_ADDR_LEN);
		break;
	case WLC_E_LINK:
	case WLC_E_NDIS_LINK:
		cmd = SIOCGIWAP;
		if (!(flags & WLC_EVENT_MSG_LINK)) {
			/* link down , return bssid zero */
			/*
			* set link down flag. this flag is used to remove disppeared AP
			* from specific scan cache the AP is not removed here immidately
			* to avoid race condtion between user & dongle event contexts.
			* Instead it will be removed in xxx_get_scan() user event context
			*/
#ifdef SOFTAP
#ifdef AP_ONLY
		if (ap_cfg_running) {
#else
		if (ap_cfg_running && !strncmp(dev->name, "wl0.1", 5)) {
#endif	/* AP_ONLY */
			/* notify wpa supplicant or AP cfg daemon , - AP IS DOWN */
			WL_SOFTAP(("AP DOWN %d\n", event_type));
			wl_iw_send_priv_event(priv_dev, "AP_DOWN");
		} else {
			WL_TRACE(("STA_Link Down\n"));
			g_ss_cache_ctrl.m_link_down = 1;
		}
#else		/*  STA only */
		g_ss_cache_ctrl.m_link_down = 1;
#endif /* AP_ONLY */
			WL_TRACE(("Link Down\n"));

			bzero(wrqu.addr.sa_data, ETHER_ADDR_LEN);
			bzero(&extra, ETHER_ADDR_LEN);
		}
		else {
			/* link up with AP's bssid */
			memcpy(wrqu.addr.sa_data, &e->addr, ETHER_ADDR_LEN);
			g_ss_cache_ctrl.m_link_down = 0;
			/*
			* current active bssid is saved and used to remove corresponding
			* AP from specific scan cache when link gets down
			*/
			memcpy(g_ss_cache_ctrl.m_active_bssid, &e->addr, ETHER_ADDR_LEN);
#ifdef SOFTAP

#ifdef AP_ONLY
			if (ap_cfg_running) {
#else
			if (ap_cfg_running && !strncmp(dev->name, "wl0.1", 5)) {
#endif
			/* notify wpa supplicant or AP cfg daemon , - AP IS UP */
				WL_SOFTAP(("AP UP %d\n", event_type));
				wl_iw_send_priv_event(priv_dev, "AP_UP");
			} else {
				WL_TRACE(("STA_LINK_UP\n"));
				roam_no_success_send = FALSE;
				roam_no_success = 0;
			}
#else
#endif /* SOFTAP */
			WL_TRACE(("Link UP\n"));

		}
		net_os_wake_lock_timeout_enable(dev);
		wrqu.addr.sa_family = ARPHRD_ETHER;
		break;
	case WLC_E_ACTION_FRAME:
		cmd = IWEVCUSTOM;
		if (datalen + 1 <= sizeof(extra)) {
			wrqu.data.length = datalen + 1;
			extra[0] = WLC_E_ACTION_FRAME;
			memcpy(&extra[1], data, datalen);
			WL_TRACE(("WLC_E_ACTION_FRAME len %d \n", wrqu.data.length));
		}
		break;

	case WLC_E_ACTION_FRAME_COMPLETE:
		cmd = IWEVCUSTOM;
		memcpy(&toto, data, 4);
		if (sizeof(status) + 1 <= sizeof(extra)) {
			wrqu.data.length = sizeof(status) + 1;
			extra[0] = WLC_E_ACTION_FRAME_COMPLETE;
			memcpy(&extra[1], &status, sizeof(status));
			printf("wl_iw_event status %d PacketId %d \n", status, toto);
			printf("WLC_E_ACTION_FRAME_COMPLETE len %d \n", wrqu.data.length);
		}
		break;
#endif /* WIRELESS_EXT > 14 */
#if WIRELESS_EXT > 17
	case WLC_E_MIC_ERROR: {
		struct	iw_michaelmicfailure  *micerrevt = (struct  iw_michaelmicfailure  *)&extra;
		cmd = IWEVMICHAELMICFAILURE;
		wrqu.data.length = sizeof(struct iw_michaelmicfailure);
		if (flags & WLC_EVENT_MSG_GROUP)
			micerrevt->flags |= IW_MICFAILURE_GROUP;
		else
			micerrevt->flags |= IW_MICFAILURE_PAIRWISE;
		memcpy(micerrevt->src_addr.sa_data, &e->addr, ETHER_ADDR_LEN);
		micerrevt->src_addr.sa_family = ARPHRD_ETHER;

		break;
	}
	case WLC_E_PMKID_CACHE: {
		if (data)
		{
			struct iw_pmkid_cand *iwpmkidcand = (struct iw_pmkid_cand *)&extra;
			pmkid_cand_list_t *pmkcandlist;
			pmkid_cand_t	*pmkidcand;
			int count;

			cmd = IWEVPMKIDCAND;
			pmkcandlist = data;
			count = ntoh32_ua((uint8 *)&pmkcandlist->npmkid_cand);
			ASSERT(count >= 0);
			wrqu.data.length = sizeof(struct iw_pmkid_cand);
			pmkidcand = pmkcandlist->pmkid_cand;
			while (count) {
				bzero(iwpmkidcand, sizeof(struct iw_pmkid_cand));
				if (pmkidcand->preauth)
					iwpmkidcand->flags |= IW_PMKID_CAND_PREAUTH;
				bcopy(&pmkidcand->BSSID, &iwpmkidcand->bssid.sa_data,
					ETHER_ADDR_LEN);
#ifndef SANDGATE2G
				wireless_send_event(dev, cmd, &wrqu, extra);
#endif
				pmkidcand++;
				count--;
			}
		}
		goto wl_iw_event_end;
	}
#endif /* WIRELESS_EXT > 17 */

	case WLC_E_SCAN_COMPLETE:
#if defined(WL_IW_USE_ISCAN)
		if ((g_iscan) && (g_iscan->sysioc_pid > 0) &&
			(g_iscan->iscan_state != ISCAN_STATE_IDLE))
		{
			up(&g_iscan->sysioc_sem);
		} else {
			cmd = SIOCGIWSCAN;
			wrqu.data.length = strlen(extra);
			WL_TRACE(("Event WLC_E_SCAN_COMPLETE from specific scan %d\n",
				g_iscan->iscan_state));
		}
#else
		cmd = SIOCGIWSCAN;
		wrqu.data.length = strlen(extra);
		WL_TRACE(("Event WLC_E_SCAN_COMPLETE\n"));
#endif /* defined((WL_IW_USE_ISCAN) */
	break;

	/* PNO Event */
	case WLC_E_PFN_NET_FOUND:
	{
		wlc_ssid_t	* ssid;
		ssid = (wlc_ssid_t *)data;
		WL_ERROR(("%s Event WLC_E_PFN_NET_FOUND, send %s up : find %s len=%d\n",
			__FUNCTION__, PNO_EVENT_UP, ssid->SSID, ssid->SSID_len));
		net_os_wake_lock_timeout_enable(dev);
		cmd = IWEVCUSTOM;
		memset(&wrqu, 0, sizeof(wrqu));
		strcpy(extra, PNO_EVENT_UP);
		wrqu.data.length = strlen(extra);
	}
	break;

	default:
		/* Cannot translate event */
		WL_TRACE(("Unknown Event %d: ignoring\n", event_type));
		break;
	}
#ifndef SANDGATE2G
		if (cmd) {
			if (cmd == SIOCGIWSCAN)
				wireless_send_event(dev, cmd, &wrqu, NULL);
			else
				wireless_send_event(dev, cmd, &wrqu, extra);
		}
#endif

#if WIRELESS_EXT > 14
	/* Look for WLC events that indicate a connection failure.
	 * If found, generate an IWEVCUSTOM event.
	 */
	memset(extra, 0, sizeof(extra));
	if (wl_iw_check_conn_fail(e, extra, sizeof(extra))) {
		cmd = IWEVCUSTOM;
		wrqu.data.length = strlen(extra);
#ifndef SANDGATE2G
		wireless_send_event(dev, cmd, &wrqu, extra);
#endif
	}
	goto wl_iw_event_end;	/* Avoid warning "label defined but not used" */
wl_iw_event_end:
	net_os_wake_unlock(dev);
#endif /* WIRELESS_EXT > 14 */
#endif /* WIRELESS_EXT > 13 */
}

int
wl_iw_get_wireless_stats(struct net_device *dev, struct iw_statistics *wstats)
{
	int res = 0;
	wl_cnt_t cnt;
	int phy_noise;
	int rssi;
	scb_val_t scb_val;

	phy_noise = 0;
	if ((res = dev_wlc_ioctl(dev, WLC_GET_PHY_NOISE, &phy_noise, sizeof(phy_noise))))
		goto done;

	phy_noise = dtoh32(phy_noise);
	WL_TRACE(("wl_iw_get_wireless_stats phy noise=%d\n", phy_noise));

	bzero(&scb_val, sizeof(scb_val_t));
	if ((res = dev_wlc_ioctl(dev, WLC_GET_RSSI, &scb_val, sizeof(scb_val_t))))
		goto done;

	rssi = dtoh32(scb_val.val);
	WL_TRACE(("wl_iw_get_wireless_stats rssi=%d\n", rssi));
	if (rssi <= WL_IW_RSSI_NO_SIGNAL)
		wstats->qual.qual = 0;
	else if (rssi <= WL_IW_RSSI_VERY_LOW)
		wstats->qual.qual = 1;
	else if (rssi <= WL_IW_RSSI_LOW)
		wstats->qual.qual = 2;
	else if (rssi <= WL_IW_RSSI_GOOD)
		wstats->qual.qual = 3;
	else if (rssi <= WL_IW_RSSI_VERY_GOOD)
		wstats->qual.qual = 4;
	else
		wstats->qual.qual = 5;

	/* Wraps to 0 if RSSI is 0 */
	wstats->qual.level = 0x100 + rssi;
	wstats->qual.noise = 0x100 + phy_noise;
#if WIRELESS_EXT > 18
	wstats->qual.updated |= (IW_QUAL_ALL_UPDATED | IW_QUAL_DBM);
#else
	wstats->qual.updated |= 7;
#endif /* WIRELESS_EXT > 18 */

#if WIRELESS_EXT > 11
	WL_TRACE(("wl_iw_get_wireless_stats counters=%d\n", (int)sizeof(wl_cnt_t)));

	memset(&cnt, 0, sizeof(wl_cnt_t));
	res = dev_wlc_bufvar_get(dev, "counters", (char *)&cnt, sizeof(wl_cnt_t));
	if (res)
	{
		WL_ERROR(("wl_iw_get_wireless_stats counters failed error=%d\n", res));
		goto done;
	}

	cnt.version = dtoh16(cnt.version);
	if (cnt.version != WL_CNT_T_VERSION) {
		WL_TRACE(("\tIncorrect version of counters struct: expected %d; got %d\n",
			WL_CNT_T_VERSION, cnt.version));
		goto done;
	}

	wstats->discard.nwid = 0;
	wstats->discard.code = dtoh32(cnt.rxundec);
	wstats->discard.fragment = dtoh32(cnt.rxfragerr);
	wstats->discard.retries = dtoh32(cnt.txfail);
	wstats->discard.misc = dtoh32(cnt.rxrunt) + dtoh32(cnt.rxgiant);
	wstats->miss.beacon = 0;

	WL_TRACE(("wl_iw_get_wireless_stats counters txframe=%d txbyte=%d\n",
		dtoh32(cnt.txframe), dtoh32(cnt.txbyte)));
	WL_TRACE(("wl_iw_get_wireless_stats counters rxfrmtoolong=%d\n", dtoh32(cnt.rxfrmtoolong)));
	WL_TRACE(("wl_iw_get_wireless_stats counters rxbadplcp=%d\n", dtoh32(cnt.rxbadplcp)));
	WL_TRACE(("wl_iw_get_wireless_stats counters rxundec=%d\n", dtoh32(cnt.rxundec)));
	WL_TRACE(("wl_iw_get_wireless_stats counters rxfragerr=%d\n", dtoh32(cnt.rxfragerr)));
	WL_TRACE(("wl_iw_get_wireless_stats counters txfail=%d\n", dtoh32(cnt.txfail)));
	WL_TRACE(("wl_iw_get_wireless_stats counters rxrunt=%d\n", dtoh32(cnt.rxrunt)));
	WL_TRACE(("wl_iw_get_wireless_stats counters rxgiant=%d\n", dtoh32(cnt.rxgiant)));

#endif /* WIRELESS_EXT > 11 */

done:
	return res;
}

#if defined(COEX_DHCP)
static void
wl_iw_bt_flag_set(
	struct net_device *dev,
	bool set)
{
	char buf_flag7_dhcp_on[8] = { 7, 00, 00, 00, 0x1, 0x00, 0x00, 0x00 };
	char buf_flag7_default[8] = { 7, 00, 00, 00, 0x0, 0x00, 0x00, 0x00 };

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
	rtnl_lock();
#endif

	if (set == TRUE) {
		/* Forcing bt_flag7  */
		dev_wlc_bufvar_set(dev, "btc_flags",
		                   (char *)&buf_flag7_dhcp_on[0], sizeof(buf_flag7_dhcp_on));
	} else  {
		/* Restoring default bt flag7 */
		dev_wlc_bufvar_set(dev, "btc_flags",
		                   (char *)&buf_flag7_default[0], sizeof(buf_flag7_default));
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
	rtnl_unlock();
#endif
}

static void
wl_iw_bt_timerfunc(ulong data)
{
	bt_info_t  *bt_local = (bt_info_t *)data;
	bt_local->timer_on = 0;
	WL_TRACE(("%s\n", __FUNCTION__));
	/* Enable sem */
	up(&bt_local->bt_sem);
}

static int
_bt_dhcp_sysioc_thread(void *data)
{
	DAEMONIZE("dhcp_sysioc");

	while (down_interruptible(&g_bt->bt_sem) == 0) {

		net_os_wake_lock(g_bt->dev);

		if (g_bt->timer_on) {
			g_bt->timer_on = 0;
			del_timer_sync(&g_bt->timer);
		}

		switch (g_bt->bt_state) {
			case BT_DHCP_START:
				/* DHCP started , provide OPPORTUNITY time to get DHCP address */
				g_bt->bt_state = BT_DHCP_OPPORTUNITY_WINDOW;
				mod_timer(&g_bt->timer,
				          jiffies + BT_DHCP_OPPORTUNITY_WINDOW_TIEM*HZ/1000);
				g_bt->timer_on = 1;
				break;

			case BT_DHCP_OPPORTUNITY_WINDOW:
				/* DHCP is not over yet , force BT flag  */
				WL_TRACE(("%s waiting for %d msec expired, force bt flag\n",
				          __FUNCTION__, BT_DHCP_OPPORTUNITY_WINDOW_TIEM));
				if (g_bt->dev) wl_iw_bt_flag_set(g_bt->dev, TRUE);
				g_bt->bt_state = BT_DHCP_FLAG_FORCE_TIMEOUT;
				mod_timer(&g_bt->timer, jiffies + BT_DHCP_FLAG_FORCE_TIME*HZ/1000);
				g_bt->timer_on = 1;
				break;

			case BT_DHCP_FLAG_FORCE_TIMEOUT:
				/* DHCP is not over yet , remove BT flag anyway */
				WL_TRACE(("%s waiting for %d msec expired remove bt flag\n",
				          __FUNCTION__, BT_DHCP_FLAG_FORCE_TIME));

				if (g_bt->dev)  wl_iw_bt_flag_set(g_bt->dev, FALSE);
				g_bt->bt_state = BT_DHCP_IDLE;
				g_bt->timer_on = 0;
				break;

			default:
				WL_ERROR(("%s error g_status=%d !!!\n", __FUNCTION__,
				          g_bt->bt_state));
				if (g_bt->dev) wl_iw_bt_flag_set(g_bt->dev, FALSE);
				g_bt->bt_state = BT_DHCP_IDLE;
				g_bt->timer_on = 0;
				break;
		 }

		net_os_wake_unlock(g_bt->dev);
	}

	if (g_bt->timer_on) {
		g_bt->timer_on = 0;
		del_timer_sync(&g_bt->timer);
	}

	complete_and_exit(&g_bt->bt_exited, 0);
}

static void
wl_iw_bt_release(void)
{
	bt_info_t *bt_local = g_bt;

	if (!bt_local) {
		return;
	}

	if (bt_local->bt_pid >= 0) {
		KILL_PROC(bt_local->bt_pid, SIGTERM);
		wait_for_completion(&bt_local->bt_exited);
	}
	kfree(bt_local);
	g_bt = NULL;
}

static int
wl_iw_bt_init(struct net_device *dev)
{
	bt_info_t *bt_dhcp = NULL;

	bt_dhcp = kmalloc(sizeof(bt_info_t), GFP_KERNEL);
	if (!bt_dhcp)
		return -ENOMEM;

	memset(bt_dhcp, 0, sizeof(bt_info_t));
	bt_dhcp->bt_pid = -1;
	g_bt = bt_dhcp;
	bt_dhcp->dev = dev;
	bt_dhcp->bt_state = BT_DHCP_IDLE;

	/* Set up  timer for BT  */
	bt_dhcp->timer_ms    = 10;
	init_timer(&bt_dhcp->timer);
	bt_dhcp->timer.data = (ulong)bt_dhcp;
	bt_dhcp->timer.function = wl_iw_bt_timerfunc;

	sema_init(&bt_dhcp->bt_sem, 0);
	init_completion(&bt_dhcp->bt_exited);
	bt_dhcp->bt_pid = kernel_thread(_bt_dhcp_sysioc_thread, bt_dhcp, 0);
	if (bt_dhcp->bt_pid < 0) {
		WL_ERROR(("Failed in %s\n", __FUNCTION__));
		return -ENOMEM;
	}

	return 0;
}
#endif /*  COEX_DHCP */

int
wl_iw_attach(struct net_device *dev, void * dhdp)
{
#if defined(WL_IW_USE_ISCAN)
	int params_size = 0;
#endif /* defined(WL_IW_USE_ISCAN) */
	wl_iw_t *iw;
#if defined(WL_IW_USE_ISCAN)
	iscan_info_t *iscan = NULL;
#endif

	DHD_OS_MUTEX_INIT(&wl_cache_lock);
	DHD_OS_MUTEX_INIT(&wl_start_lock);
	DHD_OS_MUTEX_INIT(&wl_softap_lock);

#if defined(WL_IW_USE_ISCAN)
	if (!dev)
		return 0;

	/* clean all global vars */
	memset(&g_wl_iw_params, 0, sizeof(wl_iw_extra_params_t));

	/* prealloc buffer for max iscan extra params size */
#ifdef CSCAN
	params_size = (WL_SCAN_PARAMS_FIXED_SIZE + OFFSETOF(wl_iscan_params_t, params)) +
	    (WL_NUMCHANNELS * sizeof(uint16)) + WL_SCAN_PARAMS_SSID_MAX * sizeof(wlc_ssid_t);
#else
	params_size = (WL_SCAN_PARAMS_FIXED_SIZE + OFFSETOF(wl_iscan_params_t, params));
#endif /* CSCAN */
	iscan = kmalloc(sizeof(iscan_info_t), GFP_KERNEL);
	if (!iscan)
		return -ENOMEM;
	memset(iscan, 0, sizeof(iscan_info_t));

	/* Allocate memory for iscan extra params */
	iscan->iscan_ex_params_p = (wl_iscan_params_t*)kmalloc(params_size, GFP_KERNEL);
	if (!iscan->iscan_ex_params_p)
		return -ENOMEM;
	iscan->iscan_ex_param_size = params_size;
	iscan->sysioc_pid = -1;
	/* we only care about main interface so save a global here */
	g_iscan = iscan;
	iscan->dev = dev;
	iscan->iscan_state = ISCAN_STATE_IDLE;

	g_first_broadcast_scan = BROADCAST_SCAN_FIRST_IDLE;
	g_first_counter_scans = 0;
	g_iscan->scan_flag = 0;

	/* Set up the timer */
	iscan->timer_ms    = 3000;
	init_timer(&iscan->timer);
	iscan->timer.data = (ulong)iscan;
	iscan->timer.function = wl_iw_timerfunc;

	sema_init(&iscan->sysioc_sem, 0);
	init_completion(&iscan->sysioc_exited);
	iscan->sysioc_pid = kernel_thread(_iscan_sysioc_thread, iscan, 0);
	if (iscan->sysioc_pid < 0)
		return -ENOMEM;
#endif /* WL_IW_USE_ISCAN */

	iw = *(wl_iw_t **)netdev_priv(dev);
	iw->pub = (dhd_pub_t *)dhdp;
#ifdef SOFTAP
	priv_dev = dev;
#endif /* SOFTAP */
	g_scan = NULL;

	/* Get scan results : allocate 16K for now */
	g_scan = (void *)kmalloc(G_SCAN_RESULTS, GFP_KERNEL);
	if (!g_scan)
		return -ENOMEM;

	memset(g_scan, 0, G_SCAN_RESULTS);
	g_scan_specified_ssid = 0;

#if !defined(CSCAN)
	/* initialize spec scan cache controller */
	wl_iw_init_ss_cache_ctrl();
#endif /* !defined(CSCAN) */
#ifdef COEX_DHCP
	/* initialize spec bt dhcp controller */
	wl_iw_bt_init(dev);
#endif /* COEX_DHCP */


	return 0;
}

void
wl_iw_detach(void)
{
#if defined(WL_IW_USE_ISCAN)
	iscan_buf_t  *buf;
	iscan_info_t *iscan = g_iscan;

	if (!iscan)
		return;
	if (iscan->sysioc_pid >= 0) {
		KILL_PROC(iscan->sysioc_pid, SIGTERM);
		wait_for_completion(&iscan->sysioc_exited);
	}
	DHD_OS_MUTEX_LOCK(&wl_cache_lock);
	while (iscan->list_hdr) {
		buf = iscan->list_hdr->next;
		kfree(iscan->list_hdr);
		iscan->list_hdr = buf;
	}
	kfree(iscan->iscan_ex_params_p);
	kfree(iscan);
	g_iscan = NULL;
	DHD_OS_MUTEX_UNLOCK(&wl_cache_lock);
#endif /* BCMDONGLEHOST */

	if (g_scan)
		kfree(g_scan);

	g_scan = NULL;
#if !defined(CSCAN)
	wl_iw_release_ss_cache_ctrl();
#endif /* !defined(CSCAN) */
#ifdef COEX_DHCP
	wl_iw_bt_release();
#endif /* COEX_DHCP */

#ifdef SOFTAP
	if (ap_cfg_running) {
		WL_TRACE(("\n%s AP is going down\n", __FUNCTION__));
		/*  need to turn of the radio here  */
		wl_iw_send_priv_event(priv_dev, "AP_DOWN");
	}
#endif

}
