#ifndef __nanoioctl_h__
#define __nanoioctl_h__

/************************** DOCUMENTATION ************************************/
/** @defgroup wireless_extension_api Wireless Extension ioctl Definitions
 *
 * \brief The Nanoradio Linux driver provides the default set of functionality
 *        as defined by the Wireless Extension API (version 16 and onwards).
 *        In addition to the default functionality ioctl calls for Nanoradio
 *        proprietary functions have been added as described in this document.
 *
 * \section Using default Wireless Extension ioctl API
 *
 * In Linux, device parameters and configuration can be handled
 * through the ioctl system call.
 * The ioctl system call takes a file descriptor and a request type as
 * its primary arguments, along with an optional third argument which
 * contains any arguments that must be passed along with the request.
 *
 * For the wireless network interface, the Linux kernel expects
 * iw_set_ext() to be used for invoking the ioctl system call.
 *
 * The different types of requests that are applicable are defined in
 * the header file for wireless extensions.
 *
 * The example below shows the general structure of setting the SSID
 * by using iw_set_ext().
 *
 * <BR><CODE>
 * <BR>int skfd;
 * <BR>struct iwreq wrq;
 * <BR>char *essid = "TEST";
 * <BR>// Open socket
 * <BR>skfd = socket(AF_INET, SOCK_DGRAM, 0);
 * <BR>
 * <BR>// Create request data.
 * <BR>wrq.u.essid.flags = 1;
 * <BR>wrq.u.essid.pointer = (caddr_t) essid;
 * <BR>wrq.u.essid.length = strlen(essid) + 1;
 * <BR>
 * <BR>// Execute command SIOCSIWESSID (set SSID).
 * <BR>iw_set_ext(skfd, "eth1", SIOCSIWESSID, &wrq);
 * <BR>
 * <BR>close(skfd);
 * </CODE>
 *
 * \section Using Nanoradio proprietary extenstions to Wireless
 * Extension ioctl API
 *
 * Ioctl:s are passed to network devices via ifreq data structures
 * (defined in <linux/if.h>) that in simplified form looks like:
 *
 * struct ifreq {
 *    char ifr_name[IFNAMSIZ];
 *    void *ifr_data;
 * };
 *
 * where ifr_name is the name of the network interface (eg. eth1), and
 * ifr_data is a pointer to some other data (defined by the ioctl).
 *
 * As ioctl:s are performed on network sockets, a sample call would look like:
 *
 * <BR><CODE>
 * <BR>int s = socket(AF_INET, SOCK_DGRAM, 0);
 * <BR>struct ifreq ifr;
 * <BR>
 * <BR>strcpy(ifr.ifr_name, "eth1");
 * <BR>ifr.ifr_data = <something>;
 * <BR>ioctl(s, <ioctlnumber>, &ifr);
 * </CODE>
 *
 * For the nanoradio configuration api, the ioctl used is SIOCNRXIOCTL
 * (SIOCSDEVPRIVATE + 2), and the data passed in ifr_data is a pointer
 * to the following structure:
 *
 * struct nrx_ioc {
 *    uint32_t magic;      // magic cookie NRXIOCMAGIC
 *    uint32_t cmd;        // operation
 * };
 *
 * magic is a magic number used for sanity check of the passed data,
 * and cmd is a function (or sub-ioctl) number defined in <nanoioctl.h>.
 *
 * Depending on cmd, the passed structure may have more fields. For
 * instance a function taking a 32-bit integer, will use the following
 * structure:
 *
 * struct nrx_ioc_uint32_t {
 *    struct nrx_ioc ioc;
 *    uint32_t value;
 * };
 *
 * and data defining scan parameters, look like:
 *
 * struct nrx_ioc_scan_param {
 *    struct nrx_ioc ioc;
 *    uint16_t probe_delay;
 *    uint16_t min_channel_time;
 *    uint16_t max_channel_time;
 * };
 *
 * The funcion numbers themselves are defined in a way that makes it
 * possible to distinguish the size (and indirectly the type) of the
 * data passed, as well as data direction.
 *
 * <BR><CODE>
 * <BR>#define NRXIORJOINTIMEOUT    NRXIOR('P', 2, uint32_t)
 * <BR>#define NRXIOWJOINTIMEOUT    NRXIOW('P', 2, uint32_t)
 * <BR>
 * <BR>#define NRXIORSCANPARAM      NRXIOR('P', 3, struct nrx_ioc_scan_param)
 * <BR>#define NRXIOWSCANPARAM      NRXIOW('P', 3, struct nrx_ioc_scan_param)
 * </CODE>
 *
 * To set a the join timeout value, the following code could be used:
 *
 * <BR><CODE>
 * <BR>int s = socket(AF_INET, SOCK_DGRAM, 0);
 * <BR>struct ifreq ifr;
 * <BR>struct nrx_ioc_uint32_t param;
 * <BR>
 * <BR>strcpy(ifr.ifr_name, "eth1");
 * <BR>ifr.ifr_data = &param;
 * <BR>param.ioc.magic = NRXIOCMAGIC;
 * <BR>param.ioc.cmd = NRXIOWJOINTIMEOUT;
 * <BR>param.value = 500;
 * <BR>
 * <BR>ioctl(s, SIOCNRXIOCTL, &ifr);
 * </CODE>
******************************************************************************/

#ifndef M80211_IE_MAX_LENGTH_SSID
#define M80211_IE_MAX_LENGTH_SSID 32
#endif

struct nanoioctl {
   uint32_t magic;
#define NR_MAGIC 0x6e616e70
   uint32_t tid;
   size_t length;
   unsigned char data[512];
};


#define SIOCNRXRAWTX            (SIOCDEVPRIVATE)
#define SIOCNRXRAWRX            (SIOCDEVPRIVATE + 1)
#define SIOCNRXIOCTL            (SIOCDEVPRIVATE + 2)

//unsupported
#if 0
#define SIOCNRGETREPLY          (SIOCDEVPRIVATE + 3)
#define SIOCNRSENDCOMMAND       (SIOCDEVPRIVATE + 4)
#endif

/* ============================================================ */

/* please note what these calls represent in terms of C functions:
   NRXIO   -> void func(void)
   NRXIOR  -> type func(void)
   NRXIOW  -> void func(type)
   NRXIOWR -> type func(type)
*/
   

#define NRXVOID         0U
#define NRXIN           1U
#define NRXOUT          2U
#define NRXINOUT        (NRXIN|NRXOUT)

#define NRXIOCMODE(CMD) (((CMD) >> 30) & 0x3)
#define NRXIOCSIZE(CMD) (((CMD) >> 16) & 0x3fff)

#define NRXIOC(I, G, N, L) (((I) << 30) | (((L) & 0x3fff) << 16) | ((G) << 8) | ((N) << 0))
#define NRXIO(G, N)           NRXIOC(NRXVOID, (G), (N), 0)
#define NRXIOX(D, G, N, T)    NRXIOC((D), (G), (N), sizeof(struct nrx_ioc_##T))
#define NRXIOR(G, N, T)       NRXIOX(NRXOUT, (G), (N), T)
#define NRXIOW(G, N, T)       NRXIOX(NRXIN, (G), (N), T)
#define NRXIOWR(G, N, T)      NRXIOX(NRXINOUT, (G), (N), T)

/* Helpful notes on ioctl naming etc.

 * Ioctls should be seen as a way of doing remote procedure calls to
 * the kernel. The ioctl specification defines the call signature.
 * The direction does not map to the operation itself, but rather to
 * the parameters. Short crib:
 *
 * int func(void) -> IO
 * int func(param_out) -> IOW
 * int func(param_in) -> IOR
 * int func(param_in, param_out) -> IOWR
 */

#define NRXIOCMAGIC     0x6e727800
struct nrx_ioc {
   uint32_t magic;      /* magic cookie NRXIOCMAGIC */
   uint32_t cmd;           /* operation */
};

struct nrx_ioc_uint8_t {
   struct nrx_ioc ioc;
   uint8_t value;
};

struct nrx_ioc_uint16_t {
   struct nrx_ioc ioc;
   uint16_t value;
};

struct nrx_ioc_uint32_t {
   struct nrx_ioc ioc;
   uint32_t value;
};

struct nrx_ioc_int32_t {
      struct nrx_ioc ioc;
      int32_t value;
};

struct nrx_ioc_bool {
   struct nrx_ioc ioc;
   int value;
};

struct nrx_ioc_mib_trigger {
   struct nrx_ioc ioc;
   int32_t     trig_id;
   char        mib_id[16];
   size_t      mib_id_len;
   int32_t     gating_trig_id;
   uint32_t    supv_interval;
   uint32_t    level;
   uint8_t     dir;
   uint16_t    event_count;
   uint16_t    trigmode;
};

struct nrx_ioc_verify_mib_trigger {
   struct nrx_ioc ioc;
   int32_t     trig_id;
   char        mib_id[16];
   size_t      mib_id_len;
   int         does_exist;      /* bool */
};

struct nrx_ioc_mib_value {
   struct nrx_ioc ioc;
   char         mib_id[32];
   size_t       mib_param_size;
   void         *mib_param;
};

struct nrx_ioc_console_string {
   struct nrx_ioc ioc;
   size_t         str_size;
   void          *str;
};

struct nrx_ioc_len_buf {
   struct nrx_ioc ioc;
   size_t len;
   void *buf;
};

struct nrx_ioc_power_index {
   struct nrx_ioc ioc;
   uint8_t index_qpsk;          /* 11b rates bpsk, qpsk, cck */
   uint8_t index_ofdm;          /* 11g rates ofdm */
};

struct nrx_ioc_heartbeat {
   struct nrx_ioc ioc;
   int enabled;                 
   uint32_t interval;           /*< interval in msecs */
};

struct nrx_ioc_activitytimeout {
   struct nrx_ioc ioc;
   uint32_t timeout;
   uint32_t inact_check_interval;
};

struct nrx_ioc_dscpmap {
   struct nrx_ioc ioc;
   uint8_t dscpmap[128];
};

struct nrx_ioc_arp_conf {
   struct nrx_ioc ioc;
   uint8_t mode;
   uint32_t ip;
};

#define NRX_MAX_NUM_RATES 24
struct nrx_ioc_rates {
   struct nrx_ioc ioc;
   size_t num_rates;
   uint8_t rates[NRX_MAX_NUM_RATES];
};

#define NRX_MAX_NUM_CHANS 32
struct nrx_ioc_channels {
   struct nrx_ioc ioc;
   size_t num_channels;
   uint8_t channel[NRX_MAX_NUM_CHANS];
};

struct nrx_ioc_adaptive_rate {
   struct nrx_ioc ioc;
   uint8_t level;                     /* Level bitmask: level 1 = 0x01, level 2 = 0x02, level 3 = 0x04, all = 0xFF  */
   uint8_t initial_rate;
};

struct nrx_ioc_bt_conf {
   struct nrx_ioc ioc;
   uint8_t bt_vendor;
   uint8_t pta_mode;
   uint8_t pta_def[5];
   int len;
   uint8_t antenna_dual;
   uint8_t antenna_sel0;
   uint8_t antenna_sel1;
   uint8_t antenna_level0;
   uint8_t antenna_level1;
};

struct nrx_ioc_ant_div {
   struct nrx_ioc ioc;
   uint32_t antenna_mode;
   int32_t rssi_threshold;
};

struct nrx_ioc_wmm_power_save_conf {
   struct nrx_ioc ioc;
   uint32_t tx_period;
   unsigned int be;  /* Bool */
   unsigned int bk;  /* Bool */
   unsigned int vi;  /* Bool */
   unsigned int vo;  /* Bool */
};

struct nrx_ioc_ps_conf {
   struct nrx_ioc ioc;
   unsigned int rx_all_dtim;
   unsigned int ps_poll;
   uint32_t traffic_timeout;
   uint16_t listen_interval;
};

struct nrx_ioc_power_save {
   struct nrx_ioc ioc;
   int   enable_legacy;                         /* Default TRUE */
   int   enable_uapsd;                          /* Default TRUE */
   uint32_t trigger_frame_interval_us;  /* Generate trigger frames every x micro sec */
};

struct nrx_ioc_probe_setup {
   struct nrx_ioc ioc;
   int   short_preamble;
   uint8_t  rate;
};

struct nrx_ioc_scan_conf {
   struct nrx_ioc ioc;
   int preamble;
   uint8_t rate;
   uint8_t probes_per_ch;
   uint16_t notif_pol;
   uint32_t scan_period;
   uint32_t probe_delay;
   uint16_t pa_min_ch_time;
   uint16_t pa_max_ch_time;
   uint16_t ac_min_ch_time;
   uint16_t ac_max_ch_time;
   uint32_t as_scan_period;
   uint16_t as_min_ch_time;
   uint16_t as_max_ch_time;
   uint32_t max_scan_period;
   uint32_t max_as_scan_period;
   uint8_t period_repetition;
};

struct nrx_ioc_scan_job_state {
   struct nrx_ioc ioc;
   int32_t sj_id;
   int state;
};

struct nrx_ioc_scan_add_filter {
   struct nrx_ioc ioc;
   int32_t sf_id;
   int bss_type;
   int32_t rssi_thr;
   uint32_t snr_thr;
   uint16_t threshold_type;
};

struct nrx_ioc_scan_add_job {
   struct nrx_ioc ioc;
   int32_t sj_id;
   char ssid[32];
   size_t ssid_len;
   uint8_t bssid[6];
   uint8_t scan_type;
   uint8_t channels[16];
   size_t channels_len;
   int flags;
   uint8_t prio;
   uint8_t ap_exclude;
   int sf_id;
   uint8_t run_every_nth_period;
};

struct nrx_ioc_trigger_scan {
   struct nrx_ioc ioc;
   int32_t  sj_id;
   uint16_t channel_interval;
};

struct nrx_ioc_cwin_conf {
   struct nrx_ioc ioc;
   unsigned int override;    /* Bool */
   uint8_t cwin[4][2];       /* Order: [bk, be, vi, vo][min, max] */
};

struct nrx_ioc_ier_threshold {
      struct nrx_ioc ioc;
      int thr_id;
      uint32_t ier_thr;
      uint32_t per_thr;
      uint32_t chk_period;
      uint8_t dir;
};


struct nrx_ioc_ds_conf {
   struct nrx_ioc ioc;
   uint32_t thr;
   uint16_t winsize;
};

struct nrx_ioc_ratemon {
   struct nrx_ioc ioc;
   int32_t        thr_id;
   uint32_t       sample_len;
   uint8_t        thr_level;    /* thr rate */
};

struct nrx_ioc_roam_ssid {
   struct nrx_ioc ioc;
   char ssid[32];
   size_t ssid_len;
};

struct nrx_ioc_roam_rssi_thr {
   struct nrx_ioc ioc;
   uint8_t enable;
   int32_t roam_thr;
   int32_t scan_thr;
   uint32_t margin;
};

struct nrx_ioc_roam_snr_thr {
   struct nrx_ioc ioc;
   uint8_t enable;
   uint32_t roam_thr;
   uint32_t scan_thr;
   uint32_t margin;
};

struct nrx_ioc_roam_ds_thr {
   struct nrx_ioc ioc;
   uint8_t enable;
   uint32_t roam_thr;
   uint32_t scan_thr;
};

struct nrx_ioc_roam_rate_thr {
   struct nrx_ioc ioc;
   uint8_t enable;
   uint8_t roam_thr;
   uint8_t scan_thr;
};

struct nrx_ioc_roam_net_election {
   struct nrx_ioc ioc;
   uint32_t k1;
   uint32_t k2;
};

struct nrx_ioc_roam_filter {
   struct nrx_ioc ioc;
   uint8_t enable_blacklist;
   uint8_t enable_wmm;
   uint8_t enable_ssid;
};

struct nrx_ioc_roam_conf_auth {
   struct nrx_ioc ioc;
   uint8_t enable;
   int auth_mode;
   int enc_mode;
};


/* General settings */
#define NRXIORACTIVESCAN     NRXIOR('P', 0, bool)
#define NRXIOWACTIVESCAN     NRXIOW('P', 0, bool)
#define NRXIORADAPTIVETXRATE NRXIOR('P', 1, adaptive_rate)
#define NRXIOWADAPTIVETXRATE NRXIOW('P', 1, adaptive_rate)
#define NRXIORJOINTIMEOUT    NRXIOR('P', 2, uint32_t)
#define NRXIOWJOINTIMEOUT    NRXIOW('P', 2, uint32_t)
#define NRXIORHEARTBEAT      NRXIOR('P', 3, heartbeat)
#define NRXIOWHEARTBEAT      NRXIOW('P', 3, heartbeat)
#define NRXIORMULTIDOMAIN    NRXIOR('P', 4, bool)
#define NRXIOWMULTIDOMAIN    NRXIOW('P', 4, bool)
#define NRXIORWMM            NRXIOR('P', 5, bool)
#define NRXIOWWMM            NRXIOW('P', 5, bool)
#define NRXIOWWMMPSCONF      NRXIOW('P', 6, wmm_power_save_conf)
#define NRXIOWWMMPSENABLE    NRXIOW('P', 7, uint32_t)
#define NRXIOCWMMPSDISABLE   NRXIO ('P', 7)
#define NRXIORTXPOWERINDEX   NRXIOR('P', 8, power_index)
#define NRXIOWTXPOWERINDEX   NRXIOW('P', 8, power_index)
#define NRXIORPOWERSSAVE     NRXIOR('P', 9, power_save)
#define NRXIOWPOWERSSAVE     NRXIOW('P', 9, power_save)
#define NRXIORPROBEPREAMBLE  NRXIOR('P', 10, probe_setup)
#define NRXIOWPROBEPREAMBLE  NRXIOW('P', 10, probe_setup)
#define NRXIOCREASSOCIATE    NRXIO ('P', 11)
#define NRXIOCPSENABLE       NRXIO ('P', 12)
#define NRXIORPSENABLE       NRXIOR('P', 12, bool)
#define NRXIOCPSDISABLE      NRXIO ('P', 13)
#define NRXIOWPSCONF         NRXIOW('P', 14, ps_conf)
#define NRXIOWOPRATES        NRXIOW('P', 15, rates)
#define NRXIORREGCHANNELS    NRXIOR('P', 16, channels)
#define NRXIOWREGCHANNELS    NRXIOW('P', 16, channels)
#define NRXIOWBTCOEXENABLE   NRXIOW('P', 17, bool)
#define NRXIOWBTCOEXCONF     NRXIOW('P', 18, bt_conf)
#define NRXIOWANTENNADIV     NRXIOW('P', 19, ant_div)
#define NRXIOWLINKSUPERV     NRXIOW('P', 20, bool)
#define NRXIORCORECOUNT      NRXIOR('P', 21, uint32_t)
#define NRXIOWCORECOUNT      NRXIOW('P', 21, uint32_t)
#define NRXIOWMULTIDOMAINENFORCE NRXIOW('P', 22, bool)
#define NRXIOWACTIVITYTIMEOUT NRXIOW('P', 23, activitytimeout)
#define NRXIOCACTIVITYTIMEOUT NRXIOWACTIVITYTIMEOUT /* XXX compat */
#define NRXIORDSCPMAP        NRXIOR('P', 24, dscpmap)
#define NRXIOWDSCPMAP        NRXIOW('P', 24, dscpmap)
#define NRXIOWARPCONF        NRXIOW('P', 25, arp_conf)
#define NRXIOWCONFDELAYSPREAD            NRXIOW('P', 26, ds_conf)
#define NRXIOWLINKSUPERVRXBEACONCOUNT    NRXIOW('P', 27, uint32_t)
#define NRXIOWLINKSUPERVRXBEACONTIMEOUT  NRXIOW('P', 28, uint32_t)
#define NRXIOWLINKSUPERVTXFAILCOUNT      NRXIOW('P', 29, uint32_t)
#define NRXIOWLINKSUPERVRTRIPCOUNT       NRXIOW('P', 30, uint32_t)
#define NRXIOWLINKSUPERVRTRIPSILENT      NRXIOW('P', 31, uint32_t)
#define NRXIORFRAGMENTTHR    NRXIOR('P', 32, int32_t)
#define NRXIOWFRAGMENTTHR    NRXIOW('P', 32, int32_t)
#define NRXIORRTSTHR         NRXIOR('P', 33, int32_t)
#define NRXIOWRTSTHR         NRXIOW('P', 33, int32_t)
#define NRXIOWRREGISTRY      NRXIOWR('P', 34, len_buf)
#define NRXIOCFWSUICIDE      NRXIO ('P', 35)
#define NRXIORENABLEHTRATES  NRXIOR('P', 36, bool)
#define NRXIOWENABLEHTRATES  NRXIOW('P', 36, bool)
#define NRXIOSNDADDTS        NRXIOW ('P', 37, uint8_t)
#define NRXIOSNDDELTS        NRXIOW ('P', 38, uint8_t)
#define NRXIOCSHUTDOWN       NRXIO ('P', 39)

/* IBSS */
#define NRXIORBEACONPERIOD   NRXIOR('I', 0, uint16_t)
#define NRXIOWBEACONPERIOD   NRXIOW('I', 0, uint16_t)
#define NRXIORDTIMPERIOD     NRXIOR('I', 1, uint8_t)
#define NRXIOWDTIMPERIOD     NRXIOW('I', 1, uint8_t)
#define NRXIORATIMWINDOW     NRXIOR('I', 2, uint16_t)
#define NRXIOWATIMWINDOW     NRXIOW('I', 2, uint16_t)
#define NRXIOWCWINCONF       NRXIOW('I', 3, cwin_conf) /* Contention window (Panasonic) */

/* Scan Job */
#define NRXIOWSCANCONF       NRXIOW('S', 0, scan_conf)
#define NRXIOWSCANJOBSTATE   NRXIOW('S', 1, scan_job_state)
#define NRXIOWRSCANADDFILTER NRXIOWR('S', 2, scan_add_filter)
#define NRXIOWSCANDELFILTER  NRXIOW('S', 3, int32_t)
#define NRXIOWRSCANADDJOB    NRXIOWR('S', 4, scan_add_job)
#define NRXIOWSCANDELJOB     NRXIOW('S', 5, int32_t)
#define NRXIOWSCANREGNOTIFICATION NRXIOW('S', 6, int32_t)
#define NRXIOWSCANDELNOTIFICATION NRXIOW('S', 7, int32_t)
#define NRXIOWSCANTRIGJOB    NRXIOW('S', 8, trigger_scan)
#define NRXIOWSCANLISTFLUSH  NRXIO('S', 9)
/* 10 was NRXIOWSCANADMSSIDPOOL */
/* 11 was NRXIOWSCANADMJOBSSID */

/* MIB Triggers */
/* #define NRXIOWREGMIBTRIG       NRXIOWR('M', 0, mib_trigger) *//* Old struct format */
#define NRXIOWREGMIBTRIG       NRXIOWR('M', 5, mib_trigger) /* New struct format */
#define NRXIOWDELMIBTRIG       NRXIOW('M', 1, int32_t)
#define NRXIOWRGETMIB          NRXIOWR('M', 2, mib_value)
#define NRXIOWSETMIB           NRXIOW('M', 2, mib_value)
#define NRXIOWRREGIERTRIG      NRXIOWR('M', 3, ier_threshold)
#define NRXIOWDELIERTRIG       NRXIOW('M', 3, int32_t)
#define NRXIOWRDOESTRIGEXIST   NRXIOWR('M', 4, verify_mib_trigger)
#define NRXIOWTXRATEMONENABLE  NRXIOWR('M', 6, ratemon)
#define NRXIOCTXRATEMONDISABLE NRXIOW('M', 6, int32_t)
#define NRXIOWRXRATEMONENABLE  NRXIOWR('M', 7, ratemon)
#define NRXIOCRXRATEMONDISABLE NRXIOW('M', 7, int32_t)
#define NRXIORTXRATE           NRXIOR('M', 8, uint8_t)
#define NRXIORRXRATE           NRXIOR('M', 9, uint8_t)

/* Roaming */
#define NRXIOWROAMENABLE        NRXIOW('R', 0, uint32_t)
#define NRXIOWROAMADDSSIDFILTER NRXIOW('R', 1, roam_ssid)
#define NRXIOWROAMDELSSIDFILTER NRXIOW('R', 2, roam_ssid)
#define NRXIOWROAMCONFRSSITHR   NRXIOW('R', 3, roam_rssi_thr)
#define NRXIOWROAMCONFSNRTHR   NRXIOW('R', 4, roam_snr_thr)
#define NRXIOWROAMCONFDSTHR   NRXIOW('R', 5, roam_ds_thr)
#define NRXIOWROAMCONFRATETHR   NRXIOW('R', 6, roam_rate_thr)
#define NRXIOWROAMCONFNETELECTION       NRXIOW('R', 7, roam_net_election)
#define NRXIOWROAMCONFFILTER    NRXIOW('R', 8, roam_filter)
#define NRXIOWROAMCONFAUTH    NRXIOW('R', 9, roam_conf_auth)

/* Console */
#define NRXIOWRCONSOLEREAD     NRXIOWR('C', 1, console_string)
#define NRXIOWCONSOLEWRITE     NRXIOW('C', 1, console_string)

#endif /* __nanoioctl_h__ */
