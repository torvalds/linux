/* Copyright (c) 2004 by Nanoradio AB */

/* $Id: we_dhcp.c 13112 2009-10-13 15:09:00Z joda $ */

/* This code will convert DHCP requests with the broadcast bit set to
 * requests without, and convert the response back to broadcast
 * form. The only reason to use this is if the system IP stack/DHCP
 * client can't be convinced to do this properly. Since there is a
 * performance overhead, it should probably only be used after a
 * connection.
 *
 * This is minimally tested.
 */

#include "sysdef.h"
#include "wifi_engine_internal.h"

#define TR_DHCP 0

#define GETU16(B, O) (((B)[(O)] << 8) | (B)[(O)+1])

/*
 * L2 header can either be ETHERNET II:
 0                   1                   2                   3   
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | DESTINATION ADDRESS                                           |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | DESTINATION ADDRESS CONT      | SOURCE ADDRESS                |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | SOURCE ADDRESS CONT                                           |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | ETHERNET FRAME TYPE           |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

 * Or it can come with a 802.2+SNAP header
 0                   1                   2                   3   
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | DESTINATION ADDRESS                                           |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | DESTINATION ADDRESS CONT      | SOURCE ADDRESS                |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | SOURCE ADDRESS CONT                                           |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | FRAME LENGTH                  | DSAP (AA)     | SSAP (AA)     |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | CONTROL (3)   | OUI (0)                                       |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | ETHERNET FRAME TYPE           |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

 One way to tell them apart is by looking at the type/length field
 (offset 12). The maximum length of an 802.2 frame is 1500 (0x5dc),
 while frame types start at 0x800. In practice we treat values >=
 0x600 as ethernet.
*/
#define IEEE802_3_LLC_LEN 22
#define ETHERNET2_HDR_LEN 14
/*
 * IP header from RFC791
 0                   1                   2                   3   
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |Version|  IHL  |Type of Service|          Total Length         |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |         Identification        |Flags|      Fragment Offset    |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |  Time to Live |    Protocol   |         Header Checksum       |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |                       Source Address                          |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |                    Destination Address                        |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |                    Options                    |    Padding    |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/

/* IP header field offsets */
#define IP_VHL         0
#define IP_VERSION(B) ((((B)[IP_VHL]) >> 4) & 0x0f)
#define IP_HLEN(B)    ((((B)[IP_VHL])& 0x0f) * 4)
#define IP_LENGTH      2
#define IP_FLAGS       6    /* three upper bits */
#define IP_FLAG_MF     0x20 /* more fragments */
#define IP_FRAG_OFFSET 6    /* five bits plus next byte */
#define IP_PROTOCOL    9
#define IP_CKSUM       10
#define IP_SADDR       12
#define IP_DADDR       16

/* udp header field offsets, all are two byte entities */
#define UDP_SPORT  0
#define UDP_DPORT  2
#define UDP_LENGTH 4
#define UDP_CKSUM  6

/*
 * DHCP header from RFC2131.

 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |     op (1)    |   htype (1)   |   hlen (1)    |   hops (1)    |
 +---------------+---------------+---------------+---------------+
 |                            xid (4)                            |
 +-------------------------------+-------------------------------+
 |           secs (2)            |           flags (2)           |
 +-------------------------------+-------------------------------+
 |                          ciaddr  (4)                          |
 +---------------------------------------------------------------+
 |                          yiaddr  (4)                          |
 +---------------------------------------------------------------+
 |                          siaddr  (4)                          |
 +---------------------------------------------------------------+
 |                          giaddr  (4)                          |
 +---------------------------------------------------------------+
 |                                                               |
 |                          chaddr  (16)                         |
 |                                                               |
 |                                                               |
 +---------------------------------------------------------------+
 |                                                               |
 |                          sname   (64)                         |
 +---------------------------------------------------------------+
 |                                                               |
 |                          file    (128)                        |
 +---------------------------------------------------------------+
 |                                                               |
 |                          options (variable)                   |
 +---------------------------------------------------------------+
*/

#define DHCP_OP    0
#define DHCP_HTYPE 1
#define DHCP_HLEN  2
#define DHCP_HOPS  3
#define DHCP_XID   4
#define DHCP_FLAGS 10 /* two bytes, but we care only of the first */
#define DHCP_FLAG_BROADCAST 0x80

static unsigned char wei_dhcp_active_xid[4];

/* Calculate IP checksum on a buffer. This is the one-complement of
 * the 16-bit one-comlement sum of all data. */
static uint16_t
wei_in_cksum(const void *data, size_t len, uint16_t psum)
{
   const unsigned char *ptr = data;
   size_t index = 0;
   uint32_t sum = psum;
   int need_swap = FALSE;
   
   /* make sure we're using two-byte aligned accesses below */
   if((unsigned int)ptr & 1) {
      need_swap = TRUE;
      sum = ((sum >> 8) & 0xff) + ((sum << 8) & 0xff00);
      sum += ptr[index] << 8; /* works for LE host */
      index++;
   }

   while(index + 2 <= len) {
      sum += *(const uint16_t*)(ptr + index);
      index += 2;
   }
   /* check for possible final byte */
   if(index < len) {
      sum += ptr[index]; /* works for LE host */
      index++;
   }
   /* need to do this twice if there is an overflow */
   sum = (sum >> 16) + (sum & 0xffff);
   sum = (sum >> 16) + (sum & 0xffff);

   if(need_swap)
      sum = ((sum >> 8) & 0xff) + ((sum << 8) & 0xff00);

   return ~sum & 0xffff; /* return one-complement */
}

#define IPPROTO_UDP 17
#define PORT_BOOTPS 67
#define PORT_BOOTPC 68
#define BOOTREQUEST  1
#define BOOTRESPONSE 2
#define ARPHRD_ETHER 1   


#define ETH_MIN_SIZE  14
#define IP_MIN_SIZE   20
#define DHCP_MIN_SIZE 236
#define UDP_HDR_LEN   8

static uint16_t
wei_udp_cksum(const unsigned char *ip_hdr, 
              const unsigned char *udp_hdr, 
              unsigned int udp_length)
{
   const unsigned char udp_proto[2] = { 0, IPPROTO_UDP };
   uint16_t csum;

   /* UDP pseudo header */
   csum = wei_in_cksum(ip_hdr + IP_SADDR, 8, 0); /* src & dst IP */
   csum = wei_in_cksum(udp_proto, 2, ~csum);
   csum = wei_in_cksum(udp_hdr + UDP_LENGTH, 2, ~csum);

   /* full UDP packet */
   csum = wei_in_cksum(udp_hdr, udp_length, ~csum);

   return csum;
}

static int
wei_dhcp_check_frame_type(const unsigned char *eth_hdr,
                          size_t len,
                          int is_request,
                          unsigned char **ret_ip_hdr,
                          unsigned char **ret_udp_hdr,
                          unsigned char **ret_dhcp_hdr)
{
   unsigned int eth_hdr_len;
   unsigned char *ip_hdr;
   unsigned char *udp_hdr;
   unsigned char *dhcp_hdr;
   unsigned int ip_length;
   unsigned int ip_hdr_len;
   unsigned int udp_length;

   uint16_t csum;

   if(len < ETH_MIN_SIZE + IP_MIN_SIZE + UDP_HDR_LEN + DHCP_MIN_SIZE) {
      DE_TRACE_STATIC(TR_DHCP, "short frame\n");
      return FALSE;
   }
#if 0
   if(len > 700)
      return FALSE; /* XXX assume no message size option */
#endif

   if(eth_hdr[M80211_ADDRESS_SIZE * 2] < 6) {
      /* snap */
      eth_hdr_len = IEEE802_3_LLC_LEN;
   } else {
      /* ethernet */
      eth_hdr_len = ETHERNET2_HDR_LEN;
   }

   DE_TRACE_DATA(TR_DHCP, "ETH |", eth_hdr, eth_hdr_len);

   ip_hdr = eth_hdr + eth_hdr_len;
   if(IP_VERSION(ip_hdr) != 4) {
      DE_TRACE_STATIC(TR_DHCP, "non-ipv4 frame\n");
      return FALSE; /* this is not IPv4 */
   }
   ip_hdr_len = IP_HLEN(ip_hdr);
   if(ip_hdr_len < IP_MIN_SIZE) {
      DE_TRACE_STATIC(TR_DHCP, "short ip header\n");
      return FALSE;
   }
   DE_TRACE_DATA(TR_DHCP, "IP  |", ip_hdr, ip_hdr_len);
   if(ip_hdr[IP_PROTOCOL] != IPPROTO_UDP) {
      DE_TRACE_STATIC(TR_DHCP, "non-udp frame\n");
      return FALSE;
   }
   if((ip_hdr[IP_FLAGS] & IP_FLAG_MF) != 0) {
      /* more fragments */
      DE_TRACE_STATIC(TR_DHCP, "fragmented frame\n");
      return FALSE;
   }
   if((GETU16(ip_hdr, IP_FRAG_OFFSET) & 0x1fff) != 0) {
      /* fragmented frame */
      DE_TRACE_STATIC(TR_DHCP, "fragmented frame\n");
      return FALSE;
   }
   ip_length = GETU16(ip_hdr, IP_LENGTH);
   if(ip_length < ip_hdr_len + UDP_HDR_LEN + DHCP_MIN_SIZE || 
      eth_hdr_len + ip_length > len) {
      DE_TRACE_STATIC(TR_DHCP, "bad ip length\n");
      return FALSE;
   }
   
   /* UDP */
   udp_hdr = ip_hdr + ip_hdr_len;
   DE_TRACE_DATA(TR_DHCP, "UDP |", udp_hdr, UDP_HDR_LEN);
   if(is_request) { 
      if(GETU16(udp_hdr, UDP_SPORT) != PORT_BOOTPC ||
         GETU16(udp_hdr, UDP_DPORT) != PORT_BOOTPS) {
         DE_TRACE_STATIC(TR_DHCP, "not boot port\n");
         return FALSE;
      }
   } else  {
      if(GETU16(udp_hdr, UDP_SPORT) != PORT_BOOTPS ||
         GETU16(udp_hdr, UDP_DPORT) != PORT_BOOTPC) {
         DE_TRACE_STATIC(TR_DHCP, "not boot port\n");
         return FALSE;
      }
   }
   udp_length = GETU16(udp_hdr, UDP_LENGTH);
   if(udp_length < UDP_HDR_LEN + DHCP_MIN_SIZE ||
      udp_length > ip_length - ip_hdr_len) {
      DE_TRACE_STATIC(TR_DHCP, "bad udp length\n");
      return FALSE;
   }

   /* DHCP */
   dhcp_hdr = udp_hdr + UDP_HDR_LEN;
   DE_TRACE_DATA(TR_DHCP, "DHCP|", dhcp_hdr, 32);
   if(is_request) {
      if(dhcp_hdr[DHCP_OP] != BOOTREQUEST)  {
         DE_TRACE_STATIC(TR_DHCP, "not boot request\n");
         return FALSE;
      }
   } else {
      if(dhcp_hdr[DHCP_OP] != BOOTRESPONSE) {
         DE_TRACE_STATIC(TR_DHCP, "not boot response\n");
         return FALSE;
      }
   }
   if(dhcp_hdr[DHCP_HTYPE] != ARPHRD_ETHER) {
      DE_TRACE_STATIC(TR_DHCP, "bad address type\n");
      return FALSE;
   }
   if(dhcp_hdr[DHCP_HLEN] != M80211_ADDRESS_SIZE) {
      DE_TRACE_STATIC(TR_DHCP, "bad address size\n");
      return FALSE;
   }
   if(dhcp_hdr[DHCP_HOPS] != 0) {
      DE_TRACE_STATIC(TR_DHCP, "non-zero hops field\n");
      return FALSE;
   }
   if(is_request) {
      if((dhcp_hdr[DHCP_FLAGS] & DHCP_FLAG_BROADCAST) != DHCP_FLAG_BROADCAST) {
         DE_TRACE_STATIC(TR_DHCP, "already unicast\n");
         return FALSE; /* already a unicast request */
      }
   } else {
      if((dhcp_hdr[DHCP_FLAGS] & DHCP_FLAG_BROADCAST) == DHCP_FLAG_BROADCAST) {
         DE_TRACE_STATIC(TR_DHCP, "already broadcast\n");
         return FALSE; /* already a broadcast response */
      }
   }
   
   /* This frame seems like what we're interested in, but since we
    * alter the frame and recalculate the checksums, we need to make
    * sure it's correct to begin with. */
   if(is_request) {
      /* assume outgoing frames are correct */
   } else {
      /* check IP header checksum */
      csum = wei_in_cksum(ip_hdr, ip_hdr_len, 0);
      if(csum != 0) {
         DE_TRACE_STATIC(TR_DHCP, "bad ip checksum\n");
         return FALSE; /* bad checksum */
      }

      if(udp_hdr[6] != 0 || udp_hdr[7] != 0) {
         /* UDP checksum present, so check it */
         csum = wei_udp_cksum(ip_hdr, udp_hdr, udp_length);
         if(csum != 0) {
            DE_TRACE_STATIC(TR_DHCP, "bad udp checksum\n");
            return FALSE; /* bad checksum */
         }
      }
   }

   *ret_ip_hdr = ip_hdr;
   *ret_udp_hdr = udp_hdr;
   *ret_dhcp_hdr = dhcp_hdr;

   return TRUE;
}

static void
wei_dhcp_request_fixup(void *frame, size_t len)
{
   unsigned char *ip_hdr;
   unsigned char *udp_hdr;
   unsigned char *dhcp_hdr;
   uint16_t csum;

   if(!wei_dhcp_check_frame_type(frame, len, TRUE, 
                                 &ip_hdr, &udp_hdr, &dhcp_hdr))
      return;

   dhcp_hdr[DHCP_FLAGS] &= ~DHCP_FLAG_BROADCAST;
   
   udp_hdr[UDP_CKSUM] = udp_hdr[UDP_CKSUM + 1] = 0; /* clear checksum */
#if 1
   /* update UDP checksum, not strictly necessary */
   csum = wei_udp_cksum(ip_hdr, udp_hdr, GETU16(udp_hdr, UDP_LENGTH));
   udp_hdr[UDP_CKSUM] = csum & 0xff;
   udp_hdr[UDP_CKSUM + 1] = (csum >> 8) & 0xff;
#endif

   /* save the xid, so we can match against incoming responses */
   DE_MEMCPY(wei_dhcp_active_xid, 
             dhcp_hdr + DHCP_XID, 
             sizeof(wei_dhcp_active_xid));
}



static void
wei_dhcp_response_fixup(void *frame, size_t len)
{
   unsigned char *ip_hdr;
   unsigned char *udp_hdr;
   unsigned char *dhcp_hdr;

   uint16_t csum;

   if(!wei_dhcp_check_frame_type(frame, len, FALSE, 
                                 &ip_hdr, &udp_hdr, &dhcp_hdr))
      return;

   if(DE_MEMCMP(wei_dhcp_active_xid, 
                dhcp_hdr + DHCP_XID, 
                sizeof(wei_dhcp_active_xid)) != 0) {
      /* not the response we're waiting for */
      DE_TRACE_STATIC(TR_DHCP, "id mismatch");
      return;
   }
   dhcp_hdr[DHCP_FLAGS] |= DHCP_FLAG_BROADCAST;
   /* make broadcast frame */
   DE_MEMSET(frame, 0xff, M80211_ADDRESS_SIZE);
   DE_MEMSET(ip_hdr + IP_DADDR, 0xff, 4);

   /* calculate new IP checksum */
   ip_hdr[IP_CKSUM] = ip_hdr[IP_CKSUM + 1] = 0; /* reset checksum field */

   csum = wei_in_cksum(ip_hdr, IP_HLEN(ip_hdr), 0);
   ip_hdr[IP_CKSUM] = csum & 0xff;
   ip_hdr[IP_CKSUM + 1] = (csum >> 8) & 0xff;

   /* but don't bother with the UDP checksum */
   udp_hdr[UDP_CKSUM] = udp_hdr[UDP_CKSUM + 1] = 0; /* clear checksum */

   DE_TRACE_DATA(TR_DHCP, "ETH #", frame, 14);
   DE_TRACE_DATA(TR_DHCP, "IP  #", ip_hdr, 20);
   DE_TRACE_DATA(TR_DHCP, "UDP #", udp_hdr, 8);
   DE_TRACE_DATA(TR_DHCP, "DHCP#", dhcp_hdr, 32);
}

/* frame points to a linear frame buffer of len bytes
 * is_request should be TRUE if we expect a request (TX) frame, or
 * FALSE if a response (RX) frame
 */
int
WiFiEngine_DHCPBroadcastFixup(void *frame, size_t len, int is_request)
{
   if(is_request)
      wei_dhcp_request_fixup(frame, len);
   else
      wei_dhcp_response_fixup(frame, len);

   return WIFI_ENGINE_SUCCESS;
}
