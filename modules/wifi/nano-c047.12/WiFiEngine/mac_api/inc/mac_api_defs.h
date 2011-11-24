/*******************************************************************************

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
164 40 Kista                       http://www.wep.com
SWEDEN
*******************************************************************************/
/*----------------------------------------------------------------------------*/
/*! \file

\brief [this module handles things related to life, universe and everythig]

This module is part of the macll block.
Thing are coming in and things are coming out, bla bla bla.
]
*/
/*----------------------------------------------------------------------------*/
#ifndef MAC_API_DEFS_H
#define MAC_API_DEFS_H

/* E X P O R T E D  D E F I N E S ********************************************/

/* In hic_message_id_t */
#define MAC_API_PRIMITIVE_TYPE_REQ    0
#define MAC_API_PRIMITIVE_TYPE_RSP    0x80
#define MAC_API_PRIMITIVE_TYPE_IND    0
#define MAC_API_PRIMITIVE_TYPE_CFM    0x80
#define MAC_API_PRIMITIVE_TYPE_BIT    0x80

/* In hic_message_type_t */
#define MAC_API_MSG_DIRECTION_UL      0x80
#define MAC_API_MSG_DIRECTION_DL      0x00
#define MAC_API_MSG_DIRECTION_BIT     0x80

#define MAC_API_MESSAGE_ID_NONE       0xFF


#define HIC_MESSAGE_TYPE_DATA        0
#define HIC_MESSAGE_TYPE_MGMT        1
#define HIC_MESSAGE_TYPE_MIB         2
#define HIC_MESSAGE_TYPE_ECHO        3
#define HIC_MESSAGE_TYPE_CONSOLE     4
#define HIC_MESSAGE_TYPE_FLASH_PRG   5
#define HIC_MESSAGE_TYPE_CUSTOM      6
#define HIC_MESSAGE_TYPE_CTRL        7
#define HIC_MESSAGE_TYPE_DLM         8 
#define HIC_MESSAGE_TYPE_NUM_TYPES   9
#define HIC_MESSAGE_TYPE_AGGREGATION 0x3F



typedef uint32_t mac_api_transid_t;

typedef uint16_t hic_message_length_t;
typedef uint8_t  hic_message_type_t;            /* bit 0..6 = type, bit 7 = direction(ul/dl) */
typedef uint8_t  hic_message_id_t;              /* bit 0..5 = id,   bit 6 = reserved, bit 7 = primitive type(req/rsp/ind/cfm) */
typedef uint8_t  hic_message_ul_header_size_t;
typedef uint16_t hic_message_nr_padding_bytes_added_t;


/*------------------------------------------------------------*/

#define HIC_PUT_ULE16(_ptr, _val)   do {			\
      ((unsigned char*)(_ptr))[0] = (_val) & 0xff;		\
      ((unsigned char*)(_ptr))[1] = ((_val) >> 8) & 0xff;	\
   } while(0)

#define HIC_PUT_ULE32(_ptr, _val)   do {			\
      ((unsigned char*)(_ptr))[0] = (_val) & 0xff;		\
      ((unsigned char*)(_ptr))[1] = ((_val) >> 8) & 0xff;	\
      ((unsigned char*)(_ptr))[2] = ((_val) >> 16) & 0xff;	\
      ((unsigned char*)(_ptr))[3] = ((_val) >> 24) & 0xff;	\
   } while(0)

#define HIC_GET_ULE16(_ptr) (((const unsigned char*)(_ptr))[0]		\
			     | (((const unsigned char*)(_ptr))[1] << 8))
#define HIC_GET_ULE32(_ptr) (((const unsigned char*)(_ptr))[0]		\
			     | (((const unsigned char*)(_ptr))[1] << 8)	\
			     | (((const unsigned char*)(_ptr))[2] << 16) \
			     | (((const unsigned char*)(_ptr))[3] << 24))

/*------------------------------------------------------------*/

typedef struct
{
   hic_message_type_t                   type;
   hic_message_id_t                     id;
   hic_message_ul_header_size_t         header_size;
   uint8_t                              reserved;
   hic_message_nr_padding_bytes_added_t nr_padding_bytes_added;
}hic_message_control_t;

/* note that these use the actual length, including length of length
   field */
#define HIC_MESSAGE_LENGTH_GET(_pkt)		\
   (HIC_GET_ULE16(_pkt) + 2)
#define HIC_MESSAGE_LENGTH_SET(_pkt, _len)	\
   HIC_PUT_ULE16((_pkt), (_len) - 2)
	
#define HIC_MESSAGE_TYPE(_pkt_p)             (((uint8_t*)_pkt_p)[2])
#define HIC_MESSAGE_ID(_pkt_p)               (((uint8_t*)_pkt_p)[3])
#define HIC_MESSAGE_HDR_SIZE(_pkt_p)         (((uint8_t*)_pkt_p)[4])
#define HIC_MESSAGE_PADDING_GET(_pkt)			\
   HIC_GET_ULE16((const unsigned char*)(_pkt) + 6)
#define HIC_MESSAGE_PADDING_SET(_pkt, _len)			\
   HIC_PUT_ULE16((const unsigned char*)(_pkt) + 6, _len)

typedef struct
{
   hic_message_length_t  len;
   hic_message_control_t control;
}hic_message_header_t;

typedef struct             /* Do we really need this ? */
{
   uint16_t                length;
   hic_message_control_t   hic;
}m80211_mlme_host_header_t;

/* G L O B A L  V A R I A B L E S ********************************************/

/* I N T E R F A C E  F U N C T I O N S **************************************/
#endif    /* MAC_API_DEFS_H */
/* END OF FILE ***************************************************************/
