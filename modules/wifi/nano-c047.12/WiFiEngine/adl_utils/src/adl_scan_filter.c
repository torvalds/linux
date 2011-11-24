/** @defgroup adl_scan_filter scan filtering at MLME level
 *
 * \brief This module implements a method of filtering scan ind's at MLME level
 *
 * This module can be used to consume less memory and should be used witht the
 * hooks in mlme_proxy.c namely mlme_set_scan_filter(...)
 *
 *  @{
 */

#include "mlme_proxy.h"

struct scan_filter_entry_t;
WEI_TQ_HEAD(scan_filter_head_t, scan_filter_entry_t);

struct scan_filter_entry_t {
      m80211_ie_ssid_t ssid;
      WEI_TQ_ENTRY(scan_filter_entry_t) next;
};

static struct scan_filter_head_t scan_filter_head =
   WEI_TQ_HEAD_INITIALIZER(scan_filter_head);

/**
 * Can be used to conserve memory if one knows all
 * ssid's that are of interest
 */
void adl_scan_filter_add(char* ssid, int ssid_len)
{
   struct scan_filter_entry_t* filter;

   /*
   DE_ASSERT(ssid_len > 0 && 
             ssid_len <= M80211_IE_MAX_LENGTH_SSID);
    */

   if(ssid_len <= 0)
      return;

   if(ssid_len > M80211_IE_MAX_LENGTH_SSID)
      return;
   
   filter = (struct scan_filter_entry_t*)
      DriverEnvironment_Malloc(sizeof(struct scan_filter_entry_t));

   if(filter==NULL)
      return;

   DE_MEMSET(filter, 0, sizeof(struct scan_filter_entry_t));
   DE_MEMCPY(&filter->ssid.ssid[0], ssid, ssid_len);
   filter->ssid.hdr.len = ssid_len;

   WEI_TQ_INSERT_TAIL(&scan_filter_head, filter, next);
}

/* implements mlme_scan_filter_t */
static int adl_scan_filter_keep(mlme_bss_description_t *bss_p)
{
   struct scan_filter_entry_t* filter;

   /* only filter if there are any filters configured */
   if(WEI_TQ_EMPTY(&scan_filter_head))
      return TRUE;

   WEI_TQ_FOREACH(filter, &scan_filter_head, next)
   {
      if(wei_equal_ssid(filter->ssid, bss_p->bss_description_p->ie.ssid))
      {
         return TRUE;
      }
   }

   return FALSE;
}

void adl_scan_filter_clear(void)
{
   struct scan_filter_entry_t *filter = NULL;
   while( (filter = WEI_TQ_FIRST(&scan_filter_head)) != NULL)
   {
      WEI_TQ_REMOVE(&scan_filter_head, filter, next); 
      DriverEnvironment_Free(filter);
   }
}

void adl_scan_filter_init(void)
{
   mlme_set_scan_filter(adl_scan_filter_keep);
   adl_scan_filter_clear();
}

/** @} */ /* End of adl_scan_filter group */
