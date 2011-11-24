#ifndef WEI_ASSCACHE_H
#define WEI_ASSCACHE_H

/*****************************************************************************

Copyright (c) 2004 by Nanoradio AB

This software is copyrighted by and is the sole property of Nanoradio AB.
All rights, title, ownership, or other interests in the
software remain the property of Nanoradio AB.  This software may
only be used in accordance with the corresponding license agreement.  Any
unauthorized use, duplication, transmission, distribution, or disclosure of
this software is expressly forbidden.

This Copyright notice may not be removed or modified without prior written
consent of Nanoradio AB.

Nanoradio AB reserves the right to modify this software without
notice.

Nanoradio AB
Torshamnsgatan 39                       info@nanoradio.se
164 40 Kista                            http://www.nanoradio.se
SWEDEN

Module Description :
==================

This module implements a association request/response cache
for WPA/RSN support in WiFiEngine.

*****************************************************************************/

void wei_asscache_init(void);

void wei_asscache_free(void);

void wei_asscache_add_req(m80211_mlme_associate_req_t *req);

m80211_mlme_associate_req_t *wei_asscache_get_req(void);

void wei_asscache_add_cfm(m80211_mlme_associate_cfm_t *cfm);

m80211_mlme_associate_cfm_t *wei_asscache_get_cfm(void);


#endif /* WEI_ASSCACHE_H */
