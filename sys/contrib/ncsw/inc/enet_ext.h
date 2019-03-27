/* Copyright (c) 2008-2012 Freescale Semiconductor, Inc
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


/**************************************************************************//**
 @File          enet_ext.h

 @Description   Ethernet generic definitions and enums.
*//***************************************************************************/

#ifndef __ENET_EXT_H
#define __ENET_EXT_H

#include "fsl_enet.h"

#define ENET_NUM_OCTETS_PER_ADDRESS 6     /**< Number of octets (8-bit bytes) in an ethernet address */
#define ENET_GROUP_ADDR             0x01  /**< Group address mask for ethernet addresses */


/**************************************************************************//**
 @Description   Ethernet Address
*//***************************************************************************/
typedef uint8_t t_EnetAddr[ENET_NUM_OCTETS_PER_ADDRESS];

/**************************************************************************//**
 @Description   Ethernet Address Type.
*//***************************************************************************/
typedef enum e_EnetAddrType
{
    e_ENET_ADDR_TYPE_INDIVIDUAL,    /**< Individual (unicast) address */
    e_ENET_ADDR_TYPE_GROUP,         /**< Group (multicast) address */
    e_ENET_ADDR_TYPE_BROADCAST      /**< Broadcast address */
} e_EnetAddrType;

/**************************************************************************//**
 @Description   Ethernet MAC-PHY Interface
*//***************************************************************************/
typedef enum e_EnetInterface
{
    e_ENET_IF_MII   = E_ENET_IF_MII,     /**< MII interface */
    e_ENET_IF_RMII  = E_ENET_IF_RMII,    /**< RMII interface */
    e_ENET_IF_SMII  = E_ENET_IF_SMII,    /**< SMII interface */
    e_ENET_IF_GMII  = E_ENET_IF_GMII,    /**< GMII interface */
    e_ENET_IF_RGMII = E_ENET_IF_RGMII,   /**< RGMII interface */
    e_ENET_IF_TBI   = E_ENET_IF_TBI,     /**< TBI interface */
    e_ENET_IF_RTBI  = E_ENET_IF_RTBI,    /**< RTBI interface */
    e_ENET_IF_SGMII = E_ENET_IF_SGMII,   /**< SGMII interface */
    e_ENET_IF_XGMII = E_ENET_IF_XGMII,   /**< XGMII interface */
    e_ENET_IF_QSGMII= E_ENET_IF_QSGMII,  /**< QSGMII interface */
    e_ENET_IF_XFI   = E_ENET_IF_XFI      /**< XFI interface */
} e_EnetInterface;

#define ENET_IF_SGMII_BASEX       0x80000000   /**< SGMII/QSGII interface with 1000BaseX
                                                    auto-negotiation between MAC and phy
                                                    or backplane;
                                                    Note: 1000BaseX auto-negotiation relates
                                                    only to interface between MAC and phy/backplane,
                                                    SGMII phy can still synchronize with far-end phy
                                                    at 10Mbps, 100Mbps or 1000Mbps */

/**************************************************************************//**
 @Description   Ethernet Duplex Mode
*//***************************************************************************/
typedef enum e_EnetDuplexMode
{
    e_ENET_HALF_DUPLEX,             /**< Half-Duplex mode */
    e_ENET_FULL_DUPLEX              /**< Full-Duplex mode */
} e_EnetDuplexMode;

/**************************************************************************//**
 @Description   Ethernet Speed (nominal data rate)
*//***************************************************************************/
typedef enum e_EnetSpeed
{
    e_ENET_SPEED_10     = E_ENET_SPEED_10,       /**< 10 Mbps */
    e_ENET_SPEED_100    = E_ENET_SPEED_100,      /**< 100 Mbps */
    e_ENET_SPEED_1000   = E_ENET_SPEED_1000,     /**< 1000 Mbps = 1 Gbps */
    e_ENET_SPEED_2500   = E_ENET_SPEED_2500,     /**< 2500 Mbps = 2.5 Gbps */
    e_ENET_SPEED_10000  = E_ENET_SPEED_10000     /**< 10000 Mbps = 10 Gbps */
} e_EnetSpeed;

/**************************************************************************//**
 @Description   Ethernet mode (combination of MAC-PHY interface and speed)
*//***************************************************************************/
typedef enum e_EnetMode
{
    e_ENET_MODE_INVALID           = 0,                                        /**< Invalid Ethernet mode */
    e_ENET_MODE_MII_10            = (e_ENET_IF_MII   | e_ENET_SPEED_10),      /**<    10 Mbps MII   */
    e_ENET_MODE_MII_100           = (e_ENET_IF_MII   | e_ENET_SPEED_100),     /**<   100 Mbps MII   */
    e_ENET_MODE_RMII_10           = (e_ENET_IF_RMII  | e_ENET_SPEED_10),      /**<    10 Mbps RMII  */
    e_ENET_MODE_RMII_100          = (e_ENET_IF_RMII  | e_ENET_SPEED_100),     /**<   100 Mbps RMII  */
    e_ENET_MODE_SMII_10           = (e_ENET_IF_SMII  | e_ENET_SPEED_10),      /**<    10 Mbps SMII  */
    e_ENET_MODE_SMII_100          = (e_ENET_IF_SMII  | e_ENET_SPEED_100),     /**<   100 Mbps SMII  */
    e_ENET_MODE_GMII_1000         = (e_ENET_IF_GMII  | e_ENET_SPEED_1000),    /**<  1000 Mbps GMII  */
    e_ENET_MODE_RGMII_10          = (e_ENET_IF_RGMII | e_ENET_SPEED_10),      /**<    10 Mbps RGMII */
    e_ENET_MODE_RGMII_100         = (e_ENET_IF_RGMII | e_ENET_SPEED_100),     /**<   100 Mbps RGMII */
    e_ENET_MODE_RGMII_1000        = (e_ENET_IF_RGMII | e_ENET_SPEED_1000),    /**<  1000 Mbps RGMII */
    e_ENET_MODE_TBI_1000          = (e_ENET_IF_TBI   | e_ENET_SPEED_1000),    /**<  1000 Mbps TBI   */
    e_ENET_MODE_RTBI_1000         = (e_ENET_IF_RTBI  | e_ENET_SPEED_1000),    /**<  1000 Mbps RTBI  */
    e_ENET_MODE_SGMII_10          = (e_ENET_IF_SGMII | e_ENET_SPEED_10),
                                        /**< 10 Mbps SGMII with auto-negotiation between MAC and
                                             SGMII phy according to Cisco SGMII specification */
    e_ENET_MODE_SGMII_100         = (e_ENET_IF_SGMII | e_ENET_SPEED_100),
                                        /**< 100 Mbps SGMII with auto-negotiation between MAC and
                                             SGMII phy according to Cisco SGMII specification */
    e_ENET_MODE_SGMII_1000        = (e_ENET_IF_SGMII | e_ENET_SPEED_1000),
                                        /**< 1000 Mbps SGMII with auto-negotiation between MAC and
                                             SGMII phy according to Cisco SGMII specification */
    e_ENET_MODE_SGMII_2500        = (e_ENET_IF_SGMII | e_ENET_SPEED_2500),
    e_ENET_MODE_SGMII_BASEX_10    = (ENET_IF_SGMII_BASEX | e_ENET_IF_SGMII | e_ENET_SPEED_10),
                                        /**< 10 Mbps SGMII with 1000BaseX auto-negotiation between
                                             MAC and SGMII phy or backplane */
    e_ENET_MODE_SGMII_BASEX_100   = (ENET_IF_SGMII_BASEX | e_ENET_IF_SGMII | e_ENET_SPEED_100),
                                        /**< 100 Mbps SGMII with 1000BaseX auto-negotiation between
                                             MAC and SGMII phy or backplane */
    e_ENET_MODE_SGMII_BASEX_1000  = (ENET_IF_SGMII_BASEX | e_ENET_IF_SGMII | e_ENET_SPEED_1000),
                                        /**< 1000 Mbps SGMII with 1000BaseX auto-negotiation between
                                             MAC and SGMII phy or backplane */
    e_ENET_MODE_QSGMII_1000       = (e_ENET_IF_QSGMII| e_ENET_SPEED_1000),
                                        /**< 1000 Mbps QSGMII with auto-negotiation between MAC and
                                             QSGMII phy according to Cisco QSGMII specification */
    e_ENET_MODE_QSGMII_BASEX_1000 = (ENET_IF_SGMII_BASEX | e_ENET_IF_QSGMII| e_ENET_SPEED_1000),
                                        /**< 1000 Mbps QSGMII with 1000BaseX auto-negotiation between
                                             MAC and QSGMII phy or backplane */
    e_ENET_MODE_XGMII_10000       = (e_ENET_IF_XGMII | e_ENET_SPEED_10000),   /**< 10000 Mbps XGMII */
    e_ENET_MODE_XFI_10000         = (e_ENET_IF_XFI   | e_ENET_SPEED_10000)    /**< 10000 Mbps XFI */
} e_EnetMode;


#define IS_ENET_MODE_VALID(mode) \
        (((mode) == e_ENET_MODE_MII_10     ) || \
         ((mode) == e_ENET_MODE_MII_100    ) || \
         ((mode) == e_ENET_MODE_RMII_10    ) || \
         ((mode) == e_ENET_MODE_RMII_100   ) || \
         ((mode) == e_ENET_MODE_SMII_10    ) || \
         ((mode) == e_ENET_MODE_SMII_100   ) || \
         ((mode) == e_ENET_MODE_GMII_1000  ) || \
         ((mode) == e_ENET_MODE_RGMII_10   ) || \
         ((mode) == e_ENET_MODE_RGMII_100  ) || \
         ((mode) == e_ENET_MODE_RGMII_1000 ) || \
         ((mode) == e_ENET_MODE_TBI_1000   ) || \
         ((mode) == e_ENET_MODE_RTBI_1000  ) || \
         ((mode) == e_ENET_MODE_SGMII_10   ) || \
         ((mode) == e_ENET_MODE_SGMII_100  ) || \
         ((mode) == e_ENET_MODE_SGMII_1000 ) || \
         ((mode) == e_ENET_MODE_SGMII_BASEX_10   ) || \
         ((mode) == e_ENET_MODE_SGMII_BASEX_100  ) || \
         ((mode) == e_ENET_MODE_SGMII_BASEX_1000 ) || \
         ((mode) == e_ENET_MODE_XGMII_10000) || \
         ((mode) == e_ENET_MODE_QSGMII_1000) || \
         ((mode) == e_ENET_MODE_QSGMII_BASEX_1000) || \
         ((mode) == e_ENET_MODE_XFI_10000))


#define MAKE_ENET_MODE(_interface, _speed)     (e_EnetMode)((_interface) | (_speed))

#define ENET_INTERFACE_FROM_MODE(mode)          (e_EnetInterface)((mode) & 0x0FFF0000)
#define ENET_SPEED_FROM_MODE(mode)              (e_EnetSpeed)((mode) & 0x0000FFFF)

#define ENET_ADDR_TO_UINT64(_enetAddr)                  \
        (uint64_t)(((uint64_t)(_enetAddr)[0] << 40) |   \
                   ((uint64_t)(_enetAddr)[1] << 32) |   \
                   ((uint64_t)(_enetAddr)[2] << 24) |   \
                   ((uint64_t)(_enetAddr)[3] << 16) |   \
                   ((uint64_t)(_enetAddr)[4] << 8) |    \
                   ((uint64_t)(_enetAddr)[5]))

#define MAKE_ENET_ADDR_FROM_UINT64(_addr64, _enetAddr)              \
        do {                                                        \
            int i;                                                  \
            for (i=0; i < ENET_NUM_OCTETS_PER_ADDRESS; i++)         \
                (_enetAddr)[i] = (uint8_t)((_addr64) >> ((5-i)*8)); \
        } while (0)


#endif /* __ENET_EXT_H */
