/* /proc device interface for Nanoradio Linux WiFi driver. */
/* $Id: nanoproc.c 19321 2011-05-31 12:45:58Z peva $ */
#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 19)
#include <linux/config.h>
#endif
#include <linux/module.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/netdevice.h>

#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/delay.h>
#include <asm/uaccess.h>

#include "nanoutil.h"
#include "nanoparam.h"
#include "wifi_engine.h"
#include "driverenv.h"

#include "mib_defs.h"

#include "px.h"

#ifdef __arm__
#include "single.c"
#endif


static int nrx_proc_ssid_seq_show(struct seq_file *s, void *v)
{
   seq_printf(s, "\n");
   return 0;
}

static int nrx_proc_ssid_open(struct inode *ino, struct file *file)
{
   return single_open(file, nrx_proc_ssid_seq_show, NULL);
}



static ssize_t nrx_proc_ssid_write(struct file *file, 
              const char *buf, 
              size_t nbytes, 
              loff_t *ppos)
{
   struct net_device *dev = WiFiEngine_GetAdapter();
   char *p;
   char ssid[64];

   KDEBUG(TRACE, "ENTRY");
   CHECK_UNPLUG(dev);

   if (nbytes == 0) {
      return -EINVAL;
   }

   if (copy_from_user(ssid, buf, min(nbytes, sizeof ssid))) {
      KDEBUG(ERROR, "failed to copy all data");
      return -EFAULT;
   }
   if ((p = strchr(ssid, '\n')) != NULL) {
      *p = '\0';
   }

   DE_TRACE_STRING(TR_NOISE,"Set ssid %s\n", ssid);
   WiFiEngine_SetSSID(ssid, strlen(ssid));

   return nbytes;
}


static ssize_t nrx_proc_scan_write(struct file *file, const char *buf, size_t nbytes, loff_t *ppos)
{
   struct net_device *dev = WiFiEngine_GetAdapter();
   struct nrx_softc *sc = netdev_priv(dev);
   
   char msg[32];
   int val;

   KDEBUG(TRACE, "ENTRY");
   CHECK_UNPLUG(dev);

   if (nbytes < 1) {
      return -EINVAL;
   }
   if(nbytes > sizeof(msg) - 1) {
      return -EINVAL;
   }
   if(copy_from_user(msg, buf, nbytes))
      return -EFAULT;

   if(sscanf(msg, "%d", &val) != 1) {
      return -EINVAL;
   }

   switch(val) {
      case 0:
         WiFiEngine_StartNetworkScan();
         break;
      case 1:
         WiFiEngine_sac_stop();
         KDEBUG(TRACE, "wait for disconnect");
         if(wait_event_interruptible(sc->mib_wait_queue, 
                                     WiFiEngine_GetAssociatedNet() == NULL) != 0)
            return -ERESTARTSYS;
         KDEBUG(TRACE, "disconnect");
         WiFiEngine_StartNetworkScan();
         KDEBUG(TRACE, "wait for scan complete");
         if(wait_event_interruptible(sc->mib_wait_queue, 
                                     WiFiEngine_ScanInProgress() == 0) != 0)
            return -ERESTARTSYS;
         KDEBUG(TRACE, "scan complete");
         break;
      default:
         return -EINVAL;
   }
   return nbytes;
}


static void
print_rates(struct seq_file *s, 
            const char *header, 
            uint8_t *rates, 
            size_t rates_len)
{
   size_t i;
   seq_printf(s, "\t%s:", header);
   for (i = 0; i < rates_len; i++) {
      int basic = rates[i] & 0x80;
      int rate = rates[i] & 0x7f;
      seq_printf(s, "%s%d%s ", 
                 basic ? "*" : "", 
                 rate / 2,
                 (rate & 1) ? ".5": "");
   }
   seq_printf(s, "\n");
}

static const char*
get_cipher(m80211_cipher_suite_selector_t *cipher)
{
   if(memcmp(cipher->id.octet, M80211_RSN_OUI, 3) == 0 ||
      memcmp(cipher->id.octet, M80211_WPA_OUI, 3) == 0)
      switch(cipher->type) {
         case M80211_CIPHER_SUITE_GROUP:
            return "<GROUP>";
         case M80211_CIPHER_SUITE_WEP40:
            return "WEP-40";
         case M80211_CIPHER_SUITE_TKIP:
            return "TKIP";
#ifdef WAPI_SUPPORT
      case M80211_CIPHER_SUITE_WPI:
   return "WAPI";
#else
         case M80211_CIPHER_SUITE_RESERVED:
            return "<reserved>";
#endif
         case M80211_CIPHER_SUITE_CCMP:
            return "CCMP";
         case M80211_CIPHER_SUITE_WEP104:
            return "WEP-104";
      }
   return "<unknown>";
}

static const char*
get_akm(m80211_akm_suite_selector_t *akm)
{
   if(memcmp(akm->id.octet, M80211_RSN_OUI, 3) == 0 ||
      memcmp(akm->id.octet, M80211_WPA_OUI, 3) == 0)
      switch(akm->type) {
         case M80211_AKM_SUITE_802X_PMKSA:
            return "802.1X";
         case M80211_AKM_SUITE_PSK:
            return "PSK";
      }
   return "<unknown>";
}

static void
print_wpa(struct seq_file *s,
          const char *name,
          m80211_rsn_version_t version,
          m80211_cipher_suite_selector_t *group_cipher,
          int pairwise_count,
          m80211_cipher_suite_selector_t *pairwise_cipher,
          int akm_count,
          m80211_akm_suite_selector_t *akm
          )
{
   int i;
   seq_printf(s, "\t%s Version %d\n", name, version);
   seq_printf(s, "\t\tGroup Cipher : %s\n", get_cipher(group_cipher));

   seq_printf(s, "\t\tPairwise Ciphers (%d) : ", pairwise_count);
   for(i = 0; i < pairwise_count; i++) {
      seq_printf(s, "%s ", get_cipher(&pairwise_cipher[i]));
   }
   seq_printf(s, "\n");

   seq_printf(s, "\t\tAuthentication Suites (%d) : ", akm_count);
   for(i = 0; i < akm_count; i++) {
      seq_printf(s, "%s ", get_akm(&akm[i]));
   }
   seq_printf(s, "\n");
}

static void
print_crypto(struct seq_file *s, m80211_bss_description_t *bss)
{
   seq_printf(s, "\tEncryption:%s\n", (bss->capability_info & M80211_CAPABILITY_PRIVACY) ? "on" : "off");

   if(bss->ie.wpa_parameter_set.hdr.hdr.id != M80211_IE_ID_NOT_USED) {
      print_wpa(s, 
                "WPA", 
                bss->ie.wpa_parameter_set.version,
                &bss->ie.wpa_parameter_set.group_cipher_suite,
                bss->ie.wpa_parameter_set.pairwise_cipher_suite_count, 
                M80211_IE_WPA_PARAMETER_SET_GET_PAIRWISE_CIPHER_SELECTORS(&bss->ie.wpa_parameter_set),
                bss->ie.wpa_parameter_set.akm_suite_count, 
                M80211_IE_WPA_PARAMETER_SET_GET_AKM_SELECTORS(&bss->ie.wpa_parameter_set));
   }
   if(bss->ie.rsn_parameter_set.hdr.id != M80211_IE_ID_NOT_USED) {
      print_wpa(s, 
                "IEEE 802.11i/WPA2", 
                bss->ie.rsn_parameter_set.version,
                &bss->ie.rsn_parameter_set.group_cipher_suite,
                bss->ie.rsn_parameter_set.pairwise_cipher_suite_count, 
                M80211_IE_RSN_PARAMETER_SET_GET_PAIRWISE_CIPHER_SELECTORS(&bss->ie.rsn_parameter_set),
                bss->ie.rsn_parameter_set.akm_suite_count, 
                M80211_IE_RSN_PARAMETER_SET_GET_AKM_SELECTORS(&bss->ie.rsn_parameter_set));
   }
}

static void
print_country_info(struct seq_file *s, m80211_ie_country_t *c)
{
   int   i;
   int   channel_info_count;

   if(c->hdr.id != M80211_IE_ID_COUNTRY)
   {
      return;
   }

   channel_info_count = (c->hdr.len - M80211_IE_LEN_COUNTRY_STRING) / M80211_IE_CHANNEL_INFO_TRIPLET_SIZE;  

   seq_printf(s, "\tCountry string:%.3s\n", c->country_string.string);
   for(i = 0; 
       i < M80211_IE_MAX_NUM_COUNTRY_CHANNELS && i < channel_info_count; 
       i++) {
      if(c->channel_info[i].num_channels == 1)
         seq_printf(s, "\tTX Power ch %u:%ddBm\n", 
                    c->channel_info[i].first_channel, 
                    c->channel_info[i].max_tx_power);
      else
         seq_printf(s, "\tTX Power ch %u-%u:%ddBm\n", 
                    c->channel_info[i].first_channel, 
                    c->channel_info[i].first_channel + c->channel_info[i].num_channels - 1,
                    c->channel_info[i].max_tx_power);
   }
}

static void
print_wmm_acp(struct seq_file *s, AC_parameters_t *acp)
{
   uint8_t aci = (acp->ACI_ACM_AIFSN & 0x60) >> 5;
   const char *acin[] = { "AC_BE", "AC_BK", "AC_VI", "AC_VO" };
   uint8_t acm = (acp->ACI_ACM_AIFSN & 0x10) >> 4;
   uint8_t aifsn = (acp->ACI_ACM_AIFSN & 0x0f);
   uint8_t ecwmin = acp->ECWmin_ECWmax & 0x0f;
   uint8_t ecwmax = (acp->ECWmin_ECWmax & 0xf0) >> 4;
   seq_printf(s, "\t\t%s ", acin[aci]);
   if(acm)
      seq_printf(s, "ACM ");
   seq_printf(s, "AIFSN %u ", aifsn);
   seq_printf(s, "ECWmin %u ", ecwmin);
   seq_printf(s, "ECWmax %u ", ecwmax);
   seq_printf(s, "TXOP %u", acp->TXOP_Limit);
   seq_printf(s, "\n");
}

static void
print_wmm(struct seq_file *s, 
          m80211_ie_WMM_parameter_element_t *ie, 
          size_t len)
{
   seq_printf(s, "\tWMM:");
   if(ie->WMM_Protocol_Version != 1) {
      seq_printf(s, "Version %u\n", ie->WMM_Protocol_Version);
      return;
   }
   if(ie->WMM_QoS_Info & 0x80)
      seq_printf(s, "U-APSD ");
   seq_printf(s, "PSC %u ", 
              ie->WMM_QoS_Info & 0x0f);
   seq_printf(s, "\n");

   if(len == sizeof(*ie)) {
      print_wmm_acp(s, &ie->AC_BE);
      print_wmm_acp(s, &ie->AC_BK);
      print_wmm_acp(s, &ie->AC_VI);
      print_wmm_acp(s, &ie->AC_VO);
   }
}


static int 
nrx_proc_scan_seq_show(struct seq_file *s, void *v)
{
   WiFiEngine_net_t *netlist, *p;
   int c, i;
   char str[33];
   struct net_device *dev = WiFiEngine_GetAdapter();

   DE_TRACE_STATIC(TR_NOISE, "/proc scan_show handler called\n");
   CHECK_UNPLUG(dev);

   WiFiEngine_GetBSSIDList(NULL, &c);
   DE_TRACE_INT(TR_NOISE, "Allocating room for %d entries\n", c);
   netlist = kmalloc(c * sizeof *netlist, GFP_KERNEL);
   if (netlist == NULL) {
      DE_TRACE_STATIC(TR_NOISE, "Memory allocation failed\n");
      return -ENOMEM;
   }
   if (WiFiEngine_GetBSSIDList(netlist, &c) == WIFI_ENGINE_SUCCESS) {
      int cell;
      p = netlist;
      for(cell = 0; cell < c; cell++) {
         m80211_bss_description_t *bss_p = p->bss_p->bss_description_p;
         memcpy(str, bss_p->ie.ssid.ssid, bss_p->ie.ssid.hdr.len);
         str[bss_p->ie.ssid.hdr.len] = '\0';
         seq_printf(s, "Cell %02d - Address: ", cell + 1);
         for (i = 0; i < 6; i++) {
            if(i > 0)
               seq_printf(s, ":");
            seq_printf(s, "%02x", (unsigned char)p->bssId_AP.octet[i]);
         }
         seq_printf(s, "\n");

         if(M80211_IS_ESS(bss_p->capability_info)) 
            seq_printf(s, "\tMode:Master\n");
         else if(M80211_IS_IBSS(bss_p->capability_info))
            seq_printf(s, "\tMode:Ad-hoc\n");
         seq_printf(s, "\tESSID:\"%s\"\n", str);
         seq_printf(s, "\tChannel:%d\n", bss_p->ie.ds_parameter_set.channel);
         
         print_crypto(s, bss_p);

         if(M80211_WMM_INFO_ELEM_IS_PRESENT(bss_p))
            print_wmm(s, (void*)&bss_p->ie.wmm_information_element, 
                      sizeof(bss_p->ie.wmm_information_element));

         if(M80211_WMM_PARAM_ELEM_IS_PRESENT(bss_p))
            print_wmm(s, &bss_p->ie.wmm_parameter_element, 
                      sizeof(bss_p->ie.wmm_parameter_element));

#if 0
         /* FIXME: Tempory disabled since interface changed in WiFi Engine */
         if(bss_p->ie.wps_parameter_set.hdr.hdr.id == M80211_IE_ID_VENDOR_SPECIFIC) {
            seq_printf(s, "\tWPS:");
#define WSC_ID_SC_STATE           0x1044
#define WSC_ID_VERSION            0x104A
            if(bss_p->ie.wps_parameter_set.version_id == WSC_ID_VERSION &&
               bss_p->ie.wps_parameter_set.version_length == 1) {
               seq_printf(s, "Version %u ", bss_p->ie.wps_parameter_set.version_data);
            }
            if(bss_p->ie.wps_parameter_set.WPS_state_id == WSC_ID_SC_STATE &&
               bss_p->ie.wps_parameter_set.WPS_state_length == 1) {
               seq_printf(s, "State %u ", bss_p->ie.wps_parameter_set.WPS_state_data);
            }
            seq_printf(s, "\n");
         }
#endif

         if(bss_p->ie.supported_rate_set.hdr.id == M80211_IE_ID_SUPPORTED_RATES)
            print_rates(s, "Basic Rates", 
                        bss_p->ie.supported_rate_set.rates,
                        bss_p->ie.supported_rate_set.hdr.len);
         
         if(bss_p->ie.ext_supported_rate_set.hdr.id == M80211_IE_ID_EXTENDED_SUPPORTED_RATES)
            print_rates(s, "Extended Rates", 
                        bss_p->ie.ext_supported_rate_set.rates,
                        bss_p->ie.ext_supported_rate_set.hdr.len);
         
         seq_printf(s, "\tSignal level:%d dBm\n", p->bss_p->rssi_info);
         if(p->bss_p->snr_info == SNR_UNKNOWN)
            seq_printf(s, "\tSNR level:unknown\n");
         else
            seq_printf(s, "\tSNR level:%d dB\n", p->bss_p->snr_info);
         
         print_country_info(s, &bss_p->ie.country_info_set);

         seq_printf(s, "\tBeacon Period:%u\n", bss_p->beacon_period);
         seq_printf(s, "\tFail Count:%u\n", p->fail_count);

         seq_printf(s, "\tLast seen:%u ms\n",
          DriverEnvironment_tick_to_msec(DriverEnvironment_GetTicks() - p->last_heard_tick));

         p++;
      }
   }
   kfree(netlist);

   return 0;
}

static int nrx_proc_scan_open(struct inode *ino, struct file *file)
{
    return single_open(file, nrx_proc_scan_seq_show, NULL);
}

static int nrx_proc_status_seq_show(struct seq_file *s, void *v)
{
   int status;
   struct net_device *dev = WiFiEngine_GetAdapter();

   KDEBUG(TRACE, "ENTRY");
   CHECK_UNPLUG(dev);

   if (WiFiEngine_GetNetworkStatus(&status) == WIFI_ENGINE_SUCCESS) {
      if (status == 0)
         seq_printf(s, "Not connected\n");
      else
         seq_printf(s, "Connected\n");
   }
   return 0;
}

static int nrx_proc_target_seq_show(struct seq_file *s, void *v)
{
   const char* chip_names[] = {
      STR_CHIP_TYPE_UNKNOWN,
      STR_CHIP_TYPE_NRX700,
      STR_CHIP_TYPE_NRX600
   };

   struct net_device *dev = WiFiEngine_GetAdapter();
   struct nrx_softc *sc = NULL;
   chip_type_t chip_type;

   KDEBUG(TRACE, "ENTRY");
   if (dev)
      sc = netdev_priv(dev);
   DE_ASSERT(sc);
   DE_ASSERT(sc->transport);

   chip_type = sc->transport->chip_type;
   if (chip_type < ARRAY_SZ(chip_names)) {
      KDEBUG(TRACE, "chip type %d (%s)", chip_type, chip_names[chip_type]);
   }
   else {
      KDEBUG(ERROR, "Unexpected chip type %d", chip_type);
      return -EINVAL;
   }

   seq_printf(s, "%s\n", chip_names[chip_type]);
   return 0;
}

#ifdef MEM_TRACE
void memtrace_proc(struct nrx_px_softc *psc);

static struct nrx_px_entry *meminfo_entry;

static int meminfo_callout(struct nrx_px_softc *psc, int write, void *value)
{
   if(write)
      ;
   else {
      memtrace_proc(psc);
   }
   return 0;
}
#endif

static int nrx_proc_status_open(struct inode *ino, struct file *file)
{
   return single_open(file, nrx_proc_status_seq_show, NULL);
}

static int nrx_proc_target_open(struct inode *ino, struct file *file)
{
   return single_open(file, nrx_proc_target_seq_show, NULL);
}

static struct file_operations nrx_proc_ssid_ops = {
   .owner = THIS_MODULE,
   .open = nrx_proc_ssid_open,
   .write = nrx_proc_ssid_write,
   .read = seq_read,
   .llseek = seq_lseek,
   .release = single_release
};

static struct file_operations nrx_proc_scan_ops = {
   .owner = THIS_MODULE,
   .open = nrx_proc_scan_open,
   .write = nrx_proc_scan_write,
   .read = seq_read,
   .llseek = seq_lseek,
   .release = single_release
};

static struct file_operations nrx_proc_status_ops = {
   .owner = THIS_MODULE,
   .open = nrx_proc_status_open,
   .read = seq_read,
   .llseek = seq_lseek,
   .release = single_release
};

static struct file_operations nrx_proc_target_ops = {
   .owner = THIS_MODULE,
   .open = nrx_proc_target_open,
   .read = seq_read,
   .llseek = seq_lseek,
   .release = single_release
};

static int
counter_open(struct nrx_px_softc *psc, struct inode *inode, struct file *file)
{
   struct net_device *dev = nrx_px_priv(psc);
   char *id = (char*)nrx_px_private(psc);

   uint32_t u32;
   size_t len = sizeof(u32);
   
   if(nrx_get_mib(dev, id, &u32, &len) == 0) {
      nrx_px_setsize(psc, 0);
      nrx_px_printf(psc, "%u\n", le32_to_cpu(u32));
   } else {
      nrx_px_setsize(psc, 0);
      nrx_px_printf(psc, "unknown\n");
   }

   return 0;
}

static struct nrx_px_entry *
create_counter_entry(struct net_device *dev, const char *name, const char *id)
{
   struct nrx_softc *sc = netdev_priv(dev);
   
   return nrx_px_create_dynamic(dev, name, id, strlen(id) + 1,
                                0, NULL, counter_open, NULL,
                                sc->counters_dir);
}

static struct nrx_px_entry_head counter_list = 
  WEI_TQ_HEAD_INITIALIZER(counter_list);

static void
create_counter_entries(struct net_device *dev)
{
   struct nrx_px_entry *e;

   DE_ASSERT(WEI_TQ_FIRST(&counter_list) == NULL);

#define E(N, I)                        \
     e = create_counter_entry(dev, #N, MIB_dot11 ## I ## Count);  \
     e->list = &counter_list;                \
     WEI_TQ_INSERT_TAIL(e->list, e, next);

   E(tx-fragments, TransmittedFragment);
   E(tx-multicast-frames, MulticastTransmittedFrame);
   E(tx-failed, Failed);
   E(tx-retries, Retry);
   E(tx-multiple-retries, MultipleRetry);
   E(rx-frame-duplicates, FrameDuplicate);
   E(rts-success, RTSSuccess);
   E(rts-failure, RTSFailure);
   E(ack-failure, AckFailure);
   E(rx-fragments, ReceivedFragment);
   E(rx-multicast-frames, MulticastReceviedFrame);
   E(rx-fcs-error, FCSError);
   E(tx-frames, TransmittedFrame);
   E(rx-undecryptable, WEPUndecryptable);
#undef E
}

static void
remove_counter_entries(struct net_device *dev)
{
   struct nrx_softc *sc = netdev_priv(dev);
   struct nrx_px_entry *e;

   while((e = WEI_TQ_FIRST(&counter_list)) != NULL) {
      /* will automatically remove from list */
      nrx_px_remove(e, sc->counters_dir);
   }
}

extern struct nrx_px_entry fw_px_entry;
extern struct nrx_px_entry mib_px_entry;
extern struct nrx_px_entry reg_px_entry;

void create_config_entries(struct net_device *dev);
void remove_config_entries(struct net_device *dev);

int nrx_proc_setup(struct net_device *dev)
{
   struct nrx_softc *sc = netdev_priv(dev);
   struct proc_dir_entry *entry;
   char driver_path[128];
        
   DE_TRACE_STATIC(TR_NOISE, "Setting up /proc entries\n");
   /* Dir */
   snprintf(driver_path, sizeof(driver_path), "driver/%s", dev->name);
   sc->proc_dir = proc_mkdir(driver_path, NULL);
   sc->proc_dir->uid = 0;
   sc->proc_dir->gid = 0;

   /* SSID write handle */
   entry = create_proc_entry("ssid", 
                             S_IFREG | S_IRUGO,
                             sc->proc_dir);
   entry->data = dev;
   entry->proc_fops = &nrx_proc_ssid_ops;

   /* Scan handle */
   entry = create_proc_entry("scan", 
                             S_IFREG | S_IRUGO,
                             sc->proc_dir);
   entry->data = dev;
   entry->proc_fops = &nrx_proc_scan_ops; 

   /* Status read handle */
   entry = create_proc_entry("status", 
                             S_IFREG | S_IRUGO,
                             sc->proc_dir);
   entry->data = dev;
   entry->proc_fops = &nrx_proc_status_ops; 

   /* Chip-type read handle */
   entry = create_proc_entry("target", 
                             S_IFREG | S_IRUGO,
                             sc->proc_dir);
   entry->data = dev;
   entry->proc_fops = &nrx_proc_target_ops; 

   if(sc->transport->fw_download != NULL)
      nrx_px_create(&fw_px_entry, dev, sc->proc_dir);

   nrx_px_create(&mib_px_entry, dev, sc->proc_dir);
   nrx_px_create(&reg_px_entry, dev, sc->proc_dir);

#ifdef MEM_TRACE
   meminfo_entry = nrx_px_create_dynamic(dev, "meminfo", 
                                         &meminfo_callout, 
                                         sizeof(&meminfo_callout),
                                         0, NULL, nrx_px_string_open, NULL, 
                                         sc->proc_dir);
#endif

   sc->counters_dir = nrx_px_mkdir("counters", sc->proc_dir);
   create_counter_entries(dev);

   sc->config_dir = nrx_px_mkdir("config", sc->proc_dir);

   create_config_entries(dev);

   sc->core_dir = nrx_px_mkdir("core", sc->proc_dir);

   return 0;
}

int nrx_proc_cleanup(struct net_device *dev)
{
   struct nrx_softc *sc = netdev_priv(dev);
   struct nrx_px_entry *pe;
   char driver_path[128];

   DE_TRACE_STATIC(TR_NOISE, "Cleaning up /proc entries\n");
   if (sc->proc_dir->namelen == 0)
      return 0;

#ifdef MEM_TRACE
   nrx_px_remove(meminfo_entry, sc->proc_dir);
#endif
   nrx_px_remove(&reg_px_entry, sc->proc_dir);
   nrx_px_remove(&mib_px_entry, sc->proc_dir);

   if(sc->transport->fw_download != NULL)
      nrx_px_remove(&fw_px_entry, sc->proc_dir);
   remove_proc_entry("ssid", sc->proc_dir);
   remove_proc_entry("scan", sc->proc_dir);
   remove_proc_entry("status", sc->proc_dir);
   remove_proc_entry("target", sc->proc_dir);

   remove_counter_entries(dev);
   remove_proc_entry("counters", sc->proc_dir);
   sc->counters_dir = NULL;

   remove_config_entries(dev);
   remove_proc_entry("config", sc->proc_dir);
   sc->config_dir = NULL;

   while((pe = WEI_TQ_FIRST(&sc->corefiles)) != NULL)
      nrx_px_remove(pe, sc->core_dir);
   remove_proc_entry("core", sc->proc_dir);
   sc->core_dir = NULL;
   
   snprintf(driver_path, sizeof(driver_path), "driver/%s", dev->name);
   remove_proc_entry(driver_path, NULL);
   sc->proc_dir = NULL;

   return 0;
}
