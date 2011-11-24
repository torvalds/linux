/* Copyright (C) 2007 Nanoradio AB */
/* $Id: priomap.c 15534 2010-06-03 15:49:16Z joda $ */

#include "nrx_lib.h"
#include "nrx_priv.h"

/** @defgroup PRIO Priority mapping
 * @brief Manipulate TOS/DSCP/802.1d/WMM priority maps.
 * 
 * To use WMM effectively, there needs to be a way to specify which
 * traffic class each application should use. One way of doing this is
 * via the IP-TOS field. The IP-TOS is an eight bit field in the IP
 * header. It specifies four TOS (type of service) bits, and three
 * precedence bits. In modern time, the TOS field has been replaced
 * with a DSCP field (using the same field in the IP header). The DSCP
 * specifies 64 code points that can affect how packets are handled in
 * the network. It is important to note that there is no standard for
 * what each code point mean, but there are some suggestions.
 *
 * By default the driver is setup to map the DSCP Class Selector code
 * points (RFC2474) to IEEE802.1d user priority in a one-to-one
 * fashion. This means that Class Selector code point 0 is mapped to
 * user priority 0 (Best Effort) etc. 
 *
 * Note that Class Selector code point N was chosen to be identical to
 * a TOS field with no TOS bits sets and a precedence of N.
 *
 * As there is no universal mapping that suits everyone, it is possible
 * to alter the map. It is possible to specify a priority for all
 * possible values of the TOS/DSCP field.
 *
 * The following code can be used to setup a mapping for TOS precedence.
 *
 * @code
 *   nrx_priomap map;
 *   int i;
 *
 *   nrx_priomap_read(ctx, &map); // or nrx_priomap_clear(ctx, &map);
 *   for(i = 0; i < 8; i++)
 *      nrx_priomap_set(ctx, map, i << 5, i);
 *   nrx_priomap_write(ctx, map);
 * @endcode
 */

/*!
 * @ingroup PRIO
 * @brief Retrieve driver priority map.
 *
 * This retrieves the active priority map from the driver.
 *
 * @param [in]  ctx A valid nrx_context.
 * @param [out] priomap A pointer to a priomap to receive the map.
 *
 * @return Zero on success or an error code.
 *
 * <!-- NRX_API_EXCLUDE -->
 */
int
nrx_priomap_read(nrx_context ctx,
                 nrx_priomap *priomap)
{
   int ret;
   struct nrx_ioc_dscpmap param;

   NRX_ASSERT(ctx != NULL);
   NRX_CHECK(priomap != NULL);
   NRX_CHECK(sizeof(priomap->priomap) == sizeof(param.dscpmap));

   ret = nrx_nrxioctl(ctx, NRXIORDSCPMAP, &param.ioc);
   if(ret != 0)
      return ret;
   memcpy(priomap->priomap, param.dscpmap, sizeof(param.dscpmap));
   return 0;
}

/*!
 * @ingroup PRIO
 * @brief Update driver priority map.
 *
 * This updates the driver with a modified priority map.
 *
 * @param [in]  ctx A valid nrx_context.
 * @param [in]  priomap The priomap to set.
 *
 * @return Zero on success or an error code.
 *
 * <!-- NRX_API_EXCLUDE -->
 */
int
nrx_priomap_write(nrx_context ctx,
                  nrx_priomap priomap)
{
   struct nrx_ioc_dscpmap param;
   NRX_ASSERT(ctx != NULL);
   NRX_CHECK(sizeof(priomap.priomap) == sizeof(param.dscpmap));

   memcpy(param.dscpmap, priomap.priomap, sizeof(param.dscpmap));
   return nrx_nrxioctl(ctx, NRXIOWDSCPMAP, &param.ioc);
}

/*!
 * @ingroup PRIO
 * @brief Clears a priomap.
 *
 * This resets a priomap so all packets gets mapped to the BE class.
 *
 * @param [in]  ctx A valid nrx_context.
 * @param [out] priomap A pointer to the priomap to modify.
 *
 * @return Zero on success or an error code.
 *
 * <!-- NRX_API_EXCLUDE -->
 */
int
nrx_priomap_clear(nrx_context ctx, nrx_priomap *priomap)
{
   NRX_ASSERT(ctx != NULL);
   NRX_CHECK(priomap != NULL);
   memset(priomap->priomap, 0, sizeof(priomap->priomap));
   return 0;
}

/*!
 * @ingroup PRIO
 * @brief Gets an entry in a priomap.
 *
 * This returns one the priority for one particular TOS/DSCP value.
 *
 * @param [in]  ctx     A valid nrx_context.
 * @param [in]  priomap A pointer to the priomap to modify.
 * @param [in]  tos     The index to return the priority for.
 * @param [out] uprio   A pointer to the priomap to modify.
 *
 * @return Zero on success or an error code.
 *
 * <!-- NRX_API_EXCLUDE -->
 */
int
nrx_priomap_get(nrx_context ctx,
                nrx_priomap priomap, 
                uint8_t tos, 
                uint8_t *uprio)
{
   NRX_ASSERT(ctx != NULL);
   NRX_CHECK(uprio != NULL);

   *uprio = priomap.priomap[tos / 2];
   if(tos % 2) {
      *uprio >>= 4;
   }
   *uprio &= 0x07;
   return 0;
}

/*!
 * @ingroup PRIO
 * @brief Modifies a priomap.
 *
 * This modifies a priomap so that packets with a tos field set
 * to index will be mapped to 802.1d priority uprio.
 *
 * @param [in]     ctx A valid nrx_context.
 * @param [in,out] priomap A pointer to the priomap to modify.
 * @param [in]     tos Which TOS entry to modify.
 * @param [in]     uprio IEEE802.1d priority to set (0-7).
 *
 * @return Zero on success or an error code.
 *
 * <!-- NRX_API_EXCLUDE -->
 */
int
nrx_priomap_set(nrx_context ctx,
                nrx_priomap *priomap, 
                uint8_t tos, 
                uint8_t uprio)
{
   unsigned int mask = 0xf0;
   uint8_t *p;
   
   NRX_ASSERT(ctx != NULL);
   NRX_CHECK(priomap != NULL);
   NRX_CHECK(uprio < 8);

   p = &priomap->priomap[tos / 2];
   if(tos % 2) {
      uprio <<= 4;
      mask >>= 4;
   }
   *p = (*p & mask) | uprio;
   
   return 0;
}

#define DSCP(I, CP, CU) (((I) << 5) | ((CP) << 2) | (CU))

/*!
 * @ingroup PRIO
 * @brief Modify priomap.
 *
 * This modifies a priomap so that packets with a dscp code point of
 * dscp will be mapped to 802.1d priority uprio.
 *
 * @param [in]     ctx A valid nrx_context.
 * @param [in,out] priomap A pointer to the priomap to modify.
 * @param [in]     dscp Which code point to modify.
 * @param [in]     uprio IEEE802.1d priority to set (0-7).
 *
 * @return Zero on success or an error code.
 *
 * This differs from nrx_priomap_set in that you only specify a 6-bit
 * code point, instead of the complete 8-bit field. The two lower bits
 * in the DSCP field are unused.
 *
 * <!-- NRX_API_EXCLUDE -->
 */
int
nrx_priomap_set_dscp(nrx_context ctx,
                     nrx_priomap *priomap, 
                     uint8_t dscp, 
                     uint8_t uprio)
{
   int ret;
   NRX_ASSERT(ctx != NULL);
   NRX_CHECK(priomap != NULL);
   NRX_CHECK(dscp < 64);
   NRX_CHECK(uprio < 8);

   ret = nrx_priomap_set(ctx, priomap, DSCP(0, dscp, 0), uprio);
   if(ret != 0) return ret;

   ret = nrx_priomap_set(ctx, priomap, DSCP(0, dscp, 1), uprio);
   if(ret != 0) return ret;

   ret = nrx_priomap_set(ctx, priomap, DSCP(0, dscp, 2), uprio);
   if(ret != 0) return ret;
   
   ret = nrx_priomap_set(ctx, priomap, DSCP(0, dscp, 3), uprio);
   if(ret != 0) return ret;

   return 0;
}

#if 0
/* these are not ready for prime-time */
/* Class Selector */
int
nrx_priomap_mod_dscp_cs(nrx_context ctx,
                        nrx_priomap *priomap,
                        unsigned int cs,
                        unsigned int uprio)
{
   nrx_priomap_mod_dscp(priomap, DSCP(cs, 0, 0), uprio);
   
   return 0;
}

int
nrx_priomap_mod_tos_precedence(nrx_context ctx,
                               nrx_priomap *priomap)
{
   unsigned int p, t;
   unsigned int map[] = { 1, 2, 0, 3, 4, 5, 6, 7 };
   for(p = 0; p < 8; p++) {
      for(t = 0; t < 0x20; t++) {
         nrx_priomap_mod(priomap, (p << 5) | t, map[p]);
      }
   }
   
   return 0;
}

int
nrx_priomap_mod_tos_routing(nrx_context ctx,
                            nrx_priomap *priomap)
{
   unsigned int p, t;
   unsigned int map[] = { 1, 2, 0, 3, 4, 5, 6, 7 };
   for(p = 0; p < 8; p++) {
      for(t = 0; t < 0x20; t++) {
         nrx_priomap_mod(priomap, (p << 5) | t, map[p]);
      }
   }
   
   return 0;
}

/* Maps DSCP PHB AF code points to 802.1d priorities, according to
 * this table:
 *
 * Class 1: Background
 * Class 2: Best Effort
 * Class 2: Controlled Load (Video)
 * Class 4: Voice
 *
 * No traffic shaping takes place in the driver, so the drop
 * precedence has no significance
 *
 * See RFC2597.
 */
int
nrx_priomap_mod_dscp_phb_af(nrx_context ctx,
                            nrx_priomap *priomap)
{
   /* low drop prec */
   nrx_priomap_mod_dscp(priomap, DSCP(1, 2, 0), 1); /* AF11 */
   nrx_priomap_mod_dscp(priomap, DSCP(2, 2, 0), 0); /* AF21 */
   nrx_priomap_mod_dscp(priomap, DSCP(3, 2, 0), 4); /* AF31 */
   nrx_priomap_mod_dscp(priomap, DSCP(4, 2, 0), 6); /* AF41 */

   /* medium drop prec */
   nrx_priomap_mod_dscp(priomap, DSCP(1, 4, 0), 1); /* AF12 */
   nrx_priomap_mod_dscp(priomap, DSCP(2, 4, 0), 0); /* AF22 */
   nrx_priomap_mod_dscp(priomap, DSCP(3, 4, 0), 4); /* AF32 */
   nrx_priomap_mod_dscp(priomap, DSCP(4, 4, 0), 6); /* AF42 */

   /* high drop prec */
   nrx_priomap_mod_dscp(priomap, DSCP(1, 6, 0), 1); /* AF13 */
   nrx_priomap_mod_dscp(priomap, DSCP(2, 6, 0), 0); /* AF23 */
   nrx_priomap_mod_dscp(priomap, DSCP(3, 6, 0), 4); /* AF33 */
   nrx_priomap_mod_dscp(priomap, DSCP(4, 6, 0), 6); /* AF43 */

   return 0;
}

int
nrx_priomap_mod_linear_8021d(nrx_context ctx,
                             nrx_priomap *priomap)
{
   int i;
   for(i = 0; i < 8; i++)
      nrx_priomap_mod_dscp_cs(priomap, i, i);
   
   return 0;
}
#endif
