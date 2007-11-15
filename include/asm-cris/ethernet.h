/*  
 * ioctl defines for ethernet driver
 *
 * Copyright (c) 2001 Axis Communications AB
 * 
 * Author: Mikael Starvik 
 *
 */

#ifndef _CRIS_ETHERNET_H
#define _CRIS_ETHERNET_H
#define SET_ETH_SPEED_AUTO      SIOCDEVPRIVATE          /* Auto neg speed */
#define SET_ETH_SPEED_10        SIOCDEVPRIVATE+1        /* 10 Mbps */
#define SET_ETH_SPEED_100       SIOCDEVPRIVATE+2        /* 100 Mbps. */
#define SET_ETH_DUPLEX_AUTO     SIOCDEVPRIVATE+3        /* Auto neg duplex */
#define SET_ETH_DUPLEX_HALF     SIOCDEVPRIVATE+4        /* Full duplex */
#define SET_ETH_DUPLEX_FULL     SIOCDEVPRIVATE+5        /* Half duplex */
#define SET_ETH_ENABLE_LEDS     SIOCDEVPRIVATE+6        /* Enable net LEDs */
#define SET_ETH_DISABLE_LEDS    SIOCDEVPRIVATE+7        /* Disable net LEDs */
#define SET_ETH_AUTONEG         SIOCDEVPRIVATE+8
#endif /* _CRIS_ETHERNET_H */
