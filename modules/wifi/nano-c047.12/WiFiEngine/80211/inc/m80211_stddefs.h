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

\brief This module defines 802.11 message structures and parameters.

*/
/*----------------------------------------------------------------------------*/
#ifndef M80211_STDDEFS_H
#define M80211_STDDEFS_H
#include "m802_stddefs.h"

/* E X P O R T E D  D E F I N E S ********************************************/
#define M80211_NUM_AC_INSTANCES 4

#define M80211_SIFS_TIME 10

/* A slotTime in microseconds */
#define M80211_SLOTTIME_LONG   20
#define M80211_SLOTTIME_SHORT  9

#define M80211_ADDRESS_SIZE M802_ADDRESS_SIZE
#define M80211_KEY_SIZE     8

#define M0211_MIN_FRAG_THRESHOLD 256

#define M80211_MAC_PDU_SIZE_ACK 10   /* without FCS */
#define M80211_MAC_PDU_SIZE_CTS 10   /* without FCS */
#define M80211_MAC_PDU_SIZE_RTS 16   /* without FCS */
#define M80211_MAC_PDU_SIZE_MAX 2312 /* without FCS */
#define M80211_MAC_FCS_SIZE 4

#define M80211_MAC_DATA_HDR_SIZE sizeof(mac_hdr_data_t)
#define M80211_MAC_QOS_DATA_HDR_SIZE sizeof(mac_hdr_WMM_QoS_data_t)

#define M80211_PDUVAL_CONTROL_PROTVERSION 0

#define M80211_TU_EXP           10
#define M80211_TU_FRACTION_MASK ((1<<M80211_TU_EXP)-1)

/*---------------------------------------------------------------------------*/
/*-------------------------- Frame Control Macros ---------------------------*/
/*---------------------------------------------------------------------------*/

/* Frame Control Field Bit Offsets */
#define M80211_PDUOFFSET_CONTROL_PROTVERSION 0
#define M80211_PDUOFFSET_CONTROL_TYPE        2
#define M80211_PDUOFFSET_CONTROL_SUBTYPE     4
#define M80211_PDUOFFSET_CONTROL_TODS        8
#define M80211_PDUOFFSET_CONTROL_FROMDS      9
#define M80211_PDUOFFSET_CONTROL_MORE_FRAG  10
#define M80211_PDUOFFSET_CONTROL_RETRY      11
#define M80211_PDUOFFSET_CONTROL_PWR_MGMT   12
#define M80211_PDUOFFSET_CONTROL_MORE_DATA  13
#define M80211_PDUOFFSET_CONTROL_WEP        14
#define M80211_PDUOFFSET_CONTROL_ORDER      15

#define M80211_PDUOFFSET_CONTROL_FTYPE       2
#define M80211_PDUOFFSET_CONTROL_FGROUPTYPE  2
#define M80211_PDUOFFSET_CONTROL_FSUBTYPE    4

/* Frame Control Field Width */
#define M80211_PDUWIDTH_CONTROL_PROTVERSION    2
#define M80211_PDUWIDTH_CONTROL_FRAMEGROUPTYPE 2
#define M80211_PDUWIDTH_CONTROL_FRAMESUBTYPE   4
#define M80211_PDUWIDTH_CONTROL_TODS           1
#define M80211_PDUWIDTH_CONTROL_FROMDS         1
#define M80211_PDUWIDTH_CONTROL_MORE_FRAG      1
#define M80211_PDUWIDTH_CONTROL_RETRY          1
#define M80211_PDUWIDTH_CONTROL_PWR_MGMT       1
#define M80211_PDUWIDTH_CONTROL_MORE_DATA      1
#define M80211_PDUWIDTH_CONTROL_WEP            1
#define M80211_PDUWIDTH_CONTROL_ORDER          1

#define M80211_PDUWIDTH_CONTROL_FTYPE        6

/* Frame Control Frame Group Type field values */
#define M80211_FRAMEGROUPTYPE_MGMT     0x00
#define M80211_FRAMEGROUPTYPE_CTRL     0x01
#define M80211_FRAMEGROUPTYPE_DATA     0x02
#define M80211_FRAMEGROUPTYPE_RESERVED 0x03

/* Management subtype field values */

/* Control subtype field values */
#define M80211_FRAMESUBTYPE_CTRL_PSPOLL                 0x0A
#define M80211_FRAMESUBTYPE_CTRL_RTS                    0x0B
#define M80211_FRAMESUBTYPE_CTRL_CTS                    0x0C
#define M80211_FRAMESUBTYPE_CTRL_ACK                    0x0D
#define M80211_FRAMESUBTYPE_CTRL_CFEND                  0x0E
#define M80211_FRAMESUBTYPE_CTRL_CFEND_CFACK            0x0F

/* Data subtype field values */
#define M80211_FRAMESUBTYPE_DATA_DATA      0x00
#define M80211_FRAMESUBTYPE_DATA_NULL      0x04
#define M80211_FRAMESUBTYPE_QOS_DATA       0x08
#define M80211_FRAMESUBTYPE_QOS_NULL       0x0C
#define M80211_FRAMESUBTYPE_QOSMGMT_ACTION 0x0D

/* Management subtype field values */
#define M80211_FRAMESUBTYPE_MGMT_ASSOCREQ        0
#define M80211_FRAMESUBTYPE_MGMT_ASSOCRSP        1
#define M80211_FRAMESUBTYPE_MGMT_REASSOCREQ      2
#define M80211_FRAMESUBTYPE_MGMT_REASSOCRSP      3
#define M80211_FRAMESUBTYPE_MGMT_PROBEREQ        4
#define M80211_FRAMESUBTYPE_MGMT_PROBERSP        5
#define M80211_FRAMESUBTYPE_MGMT_MEASUREMENT_PILOT 6
#define M80211_FRAMESUBTYPE_MGMT_BEACON          8
#define M80211_FRAMESUBTYPE_MGMT_ATIM            9
#define M80211_FRAMESUBTYPE_MGMT_DISASSOCIATE   10
#define M80211_FRAMESUBTYPE_MGMT_AUTHENTICATE   11
#define M80211_FRAMESUBTYPE_MGMT_DEAUTHENTICATE 12
#define M80211_FRAMESUBTYPE_MGMT_ACTION         13


/* Frame Control Field Masks */
#define M80211_PDUMASK_CONTROL_PROTVERSION    ((1<<(M80211_PDUWIDTH_CONTROL_PROTVERSION))-1)
#define M80211_PDUMASK_CONTROL_FRAMEGROUPTYPE ((1<<(M80211_PDUWIDTH_CONTROL_FRAMEGROUPTYPE))-1)
#define M80211_PDUMASK_CONTROL_SUBTYPE        ((1<<(M80211_PDUWIDTH_CONTROL_FRAMESUBTYPE))-1)
#define M80211_PDUMASK_CONTROL_TODS           ((1<<(M80211_PDUWIDTH_CONTROL_TODS))-1)
#define M80211_PDUMASK_CONTROL_FROMDS         ((1<<(M80211_PDUWIDTH_CONTROL_FROMDS))-1)
#define M80211_PDUMASK_CONTROL_MORE_FRAG      ((1<<(M80211_PDUWIDTH_CONTROL_MORE_FRAG))-1)
#define M80211_PDUMASK_CONTROL_RETRY          ((1<<(M80211_PDUWIDTH_CONTROL_RETRY))-1)
#define M80211_PDUMASK_CONTROL_PWR_MGMT       ((1<<(M80211_PDUWIDTH_CONTROL_PWR_MGMT))-1)
#define M80211_PDUMASK_CONTROL_MORE_DATA      ((1<<(M80211_PDUWIDTH_CONTROL_MORE_DATA))-1)
#define M80211_PDUMASK_CONTROL_WEP            ((1<<(M80211_PDUWIDTH_CONTROL_WEP))-1)
#define M80211_PDUMASK_CONTROL_ORDER          ((1<<(M80211_PDUWIDTH_CONTROL_ORDER))-1)

#define M80211_PDUMASK_CONTROL_FTYPE       ((1<<(M80211_PDUWIDTH_CONTROL_FTYPE))-1)


/* Frame Control Access Macros */
#define M80211_SET_PROTVERSION(version) (((version) & M80211_PDUMASK_CONTROL_PROTVERSION) << M80211_PDUOFFSET_CONTROL_PROTVERSION)
#define M80211_GET_PROTVERSION(word) (((uint16_t) (word) >> M80211_PDUOFFSET_CONTROL_PROTVERSION) & M80211_PDUMASK_CONTROL_PROTVERSION)
#define M80211_CLR_PROTVERSION(word) ((uint16_t) (word) &= ~(M80211_PDUMASK_CONTROL_PROTVERSION << M80211_PDUOFFSET_CONTROL_PROTVERSION))

#define M80211_SET_FRAMEGROUPTYPE(type) (((type) & M80211_PDUMASK_CONTROL_FRAMEGROUPTYPE) << M80211_PDUOFFSET_CONTROL_TYPE)
#define M80211_GET_FRAMEGROUPTYPE(word) (((uint8_t) (word) >> M80211_PDUOFFSET_CONTROL_TYPE) & M80211_PDUMASK_CONTROL_FRAMEGROUPTYPE)
#define M80211_CLR_FRAMEGROUPTYPE(word) ((uint16_t) (word) &= ~(M80211_PDUMASK_CONTROL_FRAMEGROUPTYPE << M80211_PDUOFFSET_CONTROL_TYPE))

#define M80211_SET_FRAMESUBTYPE(subtype) (((subtype) & M80211_PDUMASK_CONTROL_SUBTYPE) << M80211_PDUOFFSET_CONTROL_SUBTYPE)
#define M80211_GET_FRAMESUBTYPE(word) (((uint8_t) (word) >> M80211_PDUOFFSET_CONTROL_SUBTYPE) & M80211_PDUMASK_CONTROL_SUBTYPE)
#define M80211_CLR_FRAMESUBTYPE(word) ((uint16_t) (word) &= ~(M80211_PDUMASK_CONTROL_SUBTYPE << M80211_PDUOFFSET_CONTROL_SUBTYPE))

#define M80211_SET_TODS(tods) (((tods) & M80211_PDUMASK_CONTROL_TODS) << M80211_PDUOFFSET_CONTROL_TODS)
#define M80211_GET_TODS(word) (((uint16_t) (word) >> M80211_PDUOFFSET_CONTROL_TODS) & M80211_PDUMASK_CONTROL_TODS)
#define M80211_CLR_TODS(word) ((uint16_t) (word) &= ~(M80211_PDUMASK_CONTROL_TODS << M80211_PDUOFFSET_CONTROL_TODS))
#define M80211_CHECK_TODS(word) ( (word) & M80211_PDUMASK_CONTROL_TODS)

#define M80211_SET_FROMDS(fromds) (((fromds) & M80211_PDUMASK_CONTROL_FROMDS) << M80211_PDUOFFSET_CONTROL_FROMDS)
#define M80211_GET_FROMDS(word) (((uint16_t) (word) >> M80211_PDUOFFSET_CONTROL_FROMDS) & M80211_PDUMASK_CONTROL_FROMDS)
#define M80211_CLR_FROMDS(word) ((uint16_t) (word) &= ~(M80211_PDUMASK_CONTROL_FROMDS << M80211_PDUOFFSET_CONTROL_FROMDS))
#define M80211_CHECK_FROMDS(word) ( (word) & M80211_PDUMASK_CONTROL_FROMDS)

#define M80211_SET_MORE_FRAG(more_frag) (((more_frag) & M80211_PDUMASK_CONTROL_MORE_FRAG) << M80211_PDUOFFSET_CONTROL_MORE_FRAG)
#define M80211_GET_MORE_FRAG(word) (((uint16_t) (word) >> M80211_PDUOFFSET_CONTROL_MORE_FRAG) & M80211_PDUMASK_CONTROL_MORE_FRAG)
#define M80211_CLR_MORE_FRAG(word) ((uint16_t) (word) &= ~(M80211_PDUMASK_CONTROL_MORE_FRAG << M80211_PDUOFFSET_CONTROL_MORE_FRAG))

#define M80211_SET_RETRY(retry) (((retry) & M80211_PDUMASK_CONTROL_RETRY) << M80211_PDUOFFSET_CONTROL_RETRY)
#define M80211_GET_RETRY(word) (((uint16_t) (word) >> M80211_PDUOFFSET_CONTROL_RETRY) & M80211_PDUMASK_CONTROL_RETRY)
#define M80211_CLR_RETRY(word) ((uint16_t) (word) &= ~(M80211_PDUMASK_CONTROL_RETRY << M80211_PDUOFFSET_CONTROL_RETRY))

#define M80211_SET_PWR_MGMT(pwr_mgmt) (((pwr_mgmt) & M80211_PDUMASK_CONTROL_PWR_MGMT) << M80211_PDUOFFSET_CONTROL_PWR_MGMT)
#define M80211_GET_PWR_MGMT(word) (((uint16_t) (word) >> M80211_PDUOFFSET_CONTROL_PWR_MGMT) & M80211_PDUMASK_CONTROL_PWR_MGMT)
#define M80211_CLR_PWR_MGMT(word) ((uint16_t) (word) &= ~(M80211_PDUMASK_CONTROL_PWR_MGMT << M80211_PDUOFFSET_CONTROL_PWR_MGMT))

#define M80211_SET_MORE_DATA(more_data) (((more_data) & M80211_PDUMASK_CONTROL_MORE_DATA) << M80211_PDUOFFSET_CONTROL_MORE_DATA)
#define M80211_GET_MORE_DATA(word) (((uint16_t) (word) >> M80211_PDUOFFSET_CONTROL_MORE_DATA) & M80211_PDUMASK_CONTROL_MORE_DATA)
#define M80211_CLR_MORE_DATA(word) ((uint16_t) (word) &= ~(M80211_PDUMASK_CONTROL_MORE_DATA << M80211_PDUOFFSET_CONTROL_MORE_DATA))

#define M80211_SET_PROTECTED_FRAME(wep) (((wep) & M80211_PDUMASK_CONTROL_WEP) << M80211_PDUOFFSET_CONTROL_WEP)
#define M80211_GET_PROTECTED_FRAME(word) (((uint16_t) (word) >> M80211_PDUOFFSET_CONTROL_WEP) & M80211_PDUMASK_CONTROL_WEP)
#define M80211_CLR_PROTECTED_FRAME(word) ((uint16_t) (word) &= ~(M80211_PDUMASK_CONTROL_WEP << M80211_PDUOFFSET_CONTROL_WEP))

#define M80211_SET_ORDER(order) (((order) & M80211_PDUMASK_CONTROL_ORDER) << M80211_PDUOFFSET_CONTROL_ORDER)
#define M80211_GET_ORDER(word) (((uint16_t) (word) >> M80211_PDUOFFSET_CONTROL_ORDER) & M80211_PDUMASK_CONTROL_ORDER)
#define M80211_CLR_ORDER(word) ((uint16_t) (word) &= ~(M80211_PDUMASK_CONTROL_ORDER << M80211_PDUOFFSET_CONTROL_ORDER))

#define M80211_SET_FTYPE(ftype) (((ftype) & M80211_PDUMASK_CONTROL_FTYPE) << M80211_PDUOFFSET_CONTROL_FTYPE)
#define M80211_GET_FTYPE(word) (((uint16_t) (word) >> M80211_PDUOFFSET_CONTROL_FTYPE) & M80211_PDUMASK_CONTROL_FTYPE)
#define M80211_CLR_FTYPE(word) ((uint16_t) (word) &= ~(M80211_PDUMASK_CONTROL_FTYPE << M80211_PDUOFFSET_CONTROL_FTYPE))

#define M80211_CHECK_FRAME_CONTROL_IS_RTS_FRAME(_word)\
        ((((_word)>>M80211_PDUOFFSET_CONTROL_FTYPE) & M80211_PDUMASK_CONTROL_FTYPE) == (M80211_FRAMEGROUPTYPE_CTRL | (M80211_FRAMESUBTYPE_CTRL_RTS<<M80211_PDUWIDTH_CONTROL_FRAMEGROUPTYPE)))


/*To report PER at the receiver, two extra fields are inserted in txgen_start.
The first word is the current frame number, the second word is the total number of frames*/
#define M80211_GET_FRAMEBODY_FIRSTWORD(_p_frame)\
   (*(uint32_t*)((uint8_t *)_p_frame + M80211_MAC_DATA_HDR_SIZE))

#define M80211_GET_FRAMEBODY_SECONDWORD(_p_frame)\
   (*(uint32_t*)((uint8_t *)_p_frame + M80211_MAC_DATA_HDR_SIZE + sizeof(uint32_t)))

/*---------------------------------------------------------------------------*/
/*------------------------ Sequence Control Macros --------------------------*/
/*---------------------------------------------------------------------------*/

/* QoS Control UP Field Bit Offsets */
#define M80211_PDUOFFSET_QOS_CTRL_UP  0
#define M80211_PDUOFFSET_QOS_CTRL_EOSP   4
#define M80211_PDUOFFSET_QOS_CTRL_ACK_POL   5

/* Sequence Control Field Bit Offsets */
#define M80211_PDUOFFSET_SEQ_CTRL_FRAG_NO  0
#define M80211_PDUOFFSET_SEQ_CTRL_SEQ_NO   4

/* Sequence Control Field Width */
#define M80211_PDUWIDTH_SEQ_CTRL_FRAG_NO   4
#define M80211_PDUWIDTH_SEQ_CTRL_SEQ_NO   12

/* QoS Control Field Width */
#define M80211_PDUWIDTH_QOS_CTRL_UP        3
#define M80211_PDUWIDTH_QOS_CTRL_EOSP      1
#define M80211_PDUWIDTH_QOS_CTRL_ACK_POL   2

/* Frame Control Field Masks */
#define M80211_PDUMASK_SEQ_CTRL_FRAG_NO ((1<<(M80211_PDUWIDTH_SEQ_CTRL_FRAG_NO))-1)
#define M80211_PDUMASK_SEQ_CTRL_SEQ_NO  ((1<<(M80211_PDUWIDTH_SEQ_CTRL_SEQ_NO))-1)

/* QoS Control Field Masks */
#define M80211_PDUMASK_QOS_CTRL_UP       ((1<<(M80211_PDUWIDTH_QOS_CTRL_UP))-1)
#define M80211_PDUMASK_QOS_CTRL_EOSP     ((1<<(M80211_PDUWIDTH_QOS_CTRL_EOSP))-1)
#define M80211_PDUMASK_QOS_CTRL_ACK_POL  ((1<<(M80211_PDUWIDTH_QOS_CTRL_ACK_POL))-1)

/* Sequence Control Access Macros */
#define M80211_SET_FRAG_NO(frag_no) (((frag_no) & M80211_PDUMASK_SEQ_CTRL_FRAG_NO) << M80211_PDUOFFSET_SEQ_CTRL_FRAG_NO)
#define M80211_GET_FRAG_NO(word) (((uint16_t) (word) >> M80211_PDUOFFSET_SEQ_CTRL_FRAG_NO) & M80211_PDUMASK_SEQ_CTRL_FRAG_NO)
#define M80211_CLR_FRAG_NO(word) ((uint16_t) (word) &= ~(M80211_PDUMASK_SEQ_CTRL_FRAG_NO << M80211_PDUOFFSET_SEQ_CTRL_FRAG_NO))

#define M80211_SET_SEQ_NO(seq_no) (((seq_no) & M80211_PDUMASK_SEQ_CTRL_SEQ_NO) << M80211_PDUOFFSET_SEQ_CTRL_SEQ_NO)
#define M80211_GET_SEQ_NO(word) (((uint16_t) (word) >> M80211_PDUOFFSET_SEQ_CTRL_SEQ_NO) & M80211_PDUMASK_SEQ_CTRL_SEQ_NO)
#define M80211_CLR_SEQ_NO(word) ((uint16_t) (word) &= ~(M80211_PDUMASK_SEQ_CTRL_SEQ_NO << M80211_PDUOFFSET_SEQ_CTRL_SEQ_NO))

#define M80211_SET_QOS_CTRL_UP(up)  (((up) & M80211_PDUMASK_QOS_CTRL_UP) << M80211_PDUOFFSET_QOS_CTRL_UP)
#define M80211_GET_QOS_CTRL_UP(word) (((uint16_t) (word) >> M80211_PDUOFFSET_QOS_CTRL_UP) & M80211_PDUMASK_QOS_CTRL_UP)

#define M80211_SET_QOS_CTRL_EOSP(esop)  (((esop) & M80211_PDUMASK_QOS_CTRL_EOSP) << M80211_PDUOFFSET_QOS_CTRL_EOSP)
#define M80211_GET_QOS_CTRL_EOSP(word) (((uint16_t) (word) >> M80211_PDUOFFSET_QOS_CTRL_EOSP) & M80211_PDUMASK_QOS_CTRL_EOSP)

#define M80211_SET_QOS_CTRL_ACK_POL(ack_pol)  (((ack_pol) & M80211_PDUMASK_QOS_CTRL_ACK_POL) << M80211_PDUOFFSET_QOS_CTRL_ACK_POL)
#define M80211_GET_QOS_CTRL_ACK_POL(word) (((uint16_t) (word) >> M80211_PDUOFFSET_QOS_CTRL_ACK_POL) & M80211_PDUMASK_QOS_CTRL_ACK_POL)

#define M80211_SET_QOS_BIT(_subtype) ((_subtype) |= M80211_FRAMESUBTYPE_QOS_DATA)

/*------------------------- Derived Macros ----------------------------------*/
#define M80211_PDUVAL_CONTROL_FTYPE_ACK\
        (M80211_FRAMEGROUPTYPE_CTRL |\
        (M80211_FRAMESUBTYPE_CTRL_ACK << M80211_PDUWIDTH_CONTROL_FRAMEGROUPTYPE))
#define M80211_PDUVAL_CONTROL_FTYPE_CTS\
        (M80211_FRAMEGROUPTYPE_CTRL |\
        (M80211_FRAMESUBTYPE_CTRL_CTS << M80211_PDUWIDTH_CONTROL_FRAMEGROUPTYPE))
#define M80211_PDUVAL_CONTROL_FTYPE_RTS\
        (M80211_FRAMEGROUPTYPE_CTRL |\
        (M80211_FRAMESUBTYPE_CTRL_RTS << M80211_PDUWIDTH_CONTROL_FRAMEGROUPTYPE))
#define M80211_PDUVAL_CONTROL_FTYPE_PSPOLL\
        (M80211_FRAMEGROUPTYPE_CTRL |\
        (M80211_FRAMESUBTYPE_CTRL_PSPOLL << M80211_PDUWIDTH_CONTROL_FRAMEGROUPTYPE))
#define M80211_PDUVAL_MGMT_FTYPE_BEACON\
        (M80211_FRAMEGROUPTYPE_MGMT |\
        (M80211_FRAMESUBTYPE_MGMT_BEACON << M80211_PDUWIDTH_CONTROL_FRAMEGROUPTYPE))
#define M80211_PDUVAL_MGMT_FTYPE_PROBERSP\
        (M80211_FRAMEGROUPTYPE_MGMT |\
        (M80211_FRAMESUBTYPE_MGMT_PROBERSP << M80211_PDUWIDTH_CONTROL_FRAMEGROUPTYPE))


#define M80211_PDUFVAL_CONTROL_PROTVERSION\
                                   (M80211_PDUVAL_CONTROL_PROTVERSION <<\
                                    M80211_PDUOFFSET_CONTROL_PROTVERSION)
#define M80211_PDUFVAL_CONTROL_FTYPE_ACK\
                                   ((M80211_PDUVAL_CONTROL_FTYPE_ACK <<\
                                    M80211_PDUOFFSET_CONTROL_FTYPE)&0xFF)
#define M80211_PDUFVAL_CONTROL_FTYPE_CTS\
                                   ((M80211_PDUVAL_CONTROL_FTYPE_CTS <<\
                                     M80211_PDUOFFSET_CONTROL_FTYPE)&0xFF)
#define M80211_PDUFVAL_CONTROL_FTYPE_RTS\
                                   ((M80211_PDUVAL_CONTROL_FTYPE_RTS <<\
                                     M80211_PDUOFFSET_CONTROL_FTYPE)&0xFF)
#define M80211_PDUFVAL_CONTROL_FTYPE_PSPOLL\
                                   ((M80211_PDUVAL_CONTROL_FTYPE_PSPOLL <<\
                                    M80211_PDUOFFSET_CONTROL_FTYPE)&0xFF)

#define MAC_MORE_FRAG(_p_frame) M80211_GET_MORE_FRAG(*(uint16_t *)(_p_frame))
        
#define M80211_PDUASSIGN_CONTROL_RETRY_SET(_p_frame)\
        {\
            uint16_t *temp = (uint16_t*)(_p_frame);\
            *temp |= (M80211_PDUMASK_CONTROL_RETRY << M80211_PDUOFFSET_CONTROL_RETRY);\
        }

#define M80211_PDUASSIGN_CONTROL_RETRY_CLEAR(_p_frame)\
        {\
            uint16_t *temp = (uint16_t*)(_p_frame);\
            *temp &= ~(M80211_PDUMASK_CONTROL_RETRY << M80211_PDUOFFSET_CONTROL_RETRY);\
        }
        
#define M80211_PDUCHECK_CONTROL_RETRY(_p_frame)\
         (*(uint16_t*)(_p_frame) &\
         (M80211_PDUMASK_CONTROL_RETRY << M80211_PDUOFFSET_CONTROL_RETRY)) != 0 ? TRUE :FALSE

#define M80211_PDUASSIGN_CONTROL_MORE_FRAG_SET(_p_frame)\
        {\
            uint16_t *temp = (uint16_t*)(_p_frame);\
            *temp |= (M80211_PDUMASK_CONTROL_MORE_FRAG << M80211_PDUOFFSET_CONTROL_MORE_FRAG);\
        }

#define M80211_PDUASSIGN_CONTROL_MORE_FRAG_CLEAR(_p_frame)\
        {\
            uint16_t *temp = (uint16_t*)(_p_frame);\
            *temp &= ~(M80211_PDUMASK_CONTROL_MORE_FRAG << M80211_PDUOFFSET_CONTROL_MORE_FRAG);\
        }

#define M80211_PDUASSIGN_CONTROL_PM_SET(_p_frame)\
        {\
            uint16_t *temp = (uint16_t*)(_p_frame);\
            *temp |= (M80211_PDUMASK_CONTROL_PWR_MGMT << M80211_PDUOFFSET_CONTROL_PWR_MGMT);\
        }

#define M80211_PDUASSIGN_CONTROL_PM_CLEAR(_p_frame)\
        {\
            uint16_t *temp = (uint16_t*)(_p_frame);\
            *temp &= ~(M80211_PDUMASK_CONTROL_PWR_MGMT << M80211_PDUOFFSET_CONTROL_PWR_MGMT);\
        }

#define M80211_PDUASSIGN_SEQ_CTRL_SET(_p_frame, seq_ctrl)\
        {\
            mac_hdr_data_t *temp = (mac_hdr_data_t*)(_p_frame);\
            temp->seq_ctrl = (seq_ctrl);\
        }

/* flags in first octet of mac address */
#define M80211_MAC_UNICAST	(0)
#define M80211_MAC_MULTICAST	(1)
#define M80211_MAC_GLOBAL	(0)
#define M80211_MAC_LOCAL	(2)

#define M80211_IS_UCAST(_p_address) (((_p_address)->octet[0] & M80211_MAC_MULTICAST) ? FALSE:TRUE)
#define M80211_SET_UCAST(_p_address) ((_p_address)->octet[0] &= ~M80211_MAC_MULTICAST)
#define M80211_SET_LOCALLY_ADMINISTRATED(_p_address) ((_p_address)->octet[0] |= M80211_MAC_LOCAL)

#define M80211_IS_BEACON(_p_frame)\
        (M80211_GET_FTYPE(((mac_hdr_t *)(_p_frame))->frame_ctrl) == M80211_PDUVAL_MGMT_FTYPE_BEACON ? TRUE : FALSE)

#define M80211_IS_ESS(_capability_field)\
        (((_capability_field) & M80211_CAPABILITY_ESS) != 0)

#define M80211_IS_IBSS(_capability_field)\
        (((_capability_field) & M80211_CAPABILITY_IBSS) != 0)

#define  M80211_WMM_INFO_ELEM_IS_PRESENT(bss_descr_ref) ((bss_descr_ref)->ie.wmm_information_element.WMM_hdr.hdr.hdr.id == M80211_IE_ID_VENDOR_SPECIFIC \
            && (bss_descr_ref)->ie.wmm_information_element.WMM_hdr.OUI_Subtype == 0x00)

#define  M80211_WMM_PARAM_ELEM_SUPPORT_PS(bss_descr_ref) ((bss_descr_ref)->ie.wmm_parameter_element.WMM_QoS_Info & 0x80)

#define  M80211_WMM_INFO_ELEM_SUPPORT_PS(bss_descr_ref) ((bss_descr_ref)->ie.wmm_information_element.WMM_QoS_Info & 0x80)
#define  M80211_WMM_PARAM_ELEM_IS_PRESENT(bss_descr_ref) ((bss_descr_ref)->ie.wmm_parameter_element.WMM_hdr.hdr.hdr.id == M80211_IE_ID_VENDOR_SPECIFIC \
            && (bss_descr_ref)->ie.wmm_parameter_element.WMM_hdr.OUI_Subtype == 0x01)
#define M80211_IS_RADIO_MEASUREMENT(_capability_field)\
        (((_capability_field) & M80211_CAPABILITY_RADIO_MEASUREMENT) != 0)

#if (DE_CCX == CFG_INCLUDED)
#define  M80211_CCX_PARAM_ELEM_IS_PRESENT(bss_descr_ref) ((bss_descr_ref)->ie.ccx_parameter_element.CCX_hdr.hdr.hdr.id == M80211_IE_ID_VENDOR_SPECIFIC \
            && (bss_descr_ref)->ie.ccx_parameter_element.CCX_hdr.OUI_Subtype == 0x03)

#define M80211_IS_RADIO_MEASUREMENT(_capability_field)\
        (((_capability_field) & M80211_CAPABILITY_RADIO_MEASUREMENT) != 0)
#endif //DE_CCX

/* E X P O R T E D  D A T A T Y P E S ****************************************/
typedef uint8_t m80211_std_rate_encoding_t;

typedef enum
{
   AC_BE=0,
   AC_BK,
   AC_VI,
   AC_VO
} m80211_access_category_t; 

typedef enum
{
   M80211_RESULT_PMCFM_SUCCESS,
   M80211_RESULT_PMCFM_INVALID_PARAMETERS,
   M80211_RESULT_PMCFM_NOT_SUPPORTED
}m80211_result_pmcfm_t;

typedef enum
{
   M80211_PM_DISABLED, 
   M80211_PM_ENABLED
}m80211_station_pm_mode_t;

typedef enum 
{
   Background_Queue, /* Lowest Priority */
   BestEffort_Queue,
   Video_Queue,
   Voice_Queue       /* Highest Priority */
} EDCA_Queue_t;
typedef uint16_t m80211_listen_interval_t;
typedef uint16_t m80211_tu16_t;

/*! 802.11 mac address */
typedef m802_mac_addr_t m80211_mac_addr_t;

/* Authenticate types */
#define M80211_AUTH_OPEN_SYSTEM 0
#define M80211_AUTH_SHARED_KEY  1


/*
 * Management MlmeStatus codes
 *
 * MlmeJoin.confirm(MlmeStatus), 
 * MlmeAuthenticate.confirm(MacAddr,AuthType,MlmeStatus), 
 * MlmeAssociate.confirm(MlmeStatus), 
 * MlmeDisassociate.confirm(MlmeStatus), 
 * MlmeDeauthenticate.confirm(MacAddr,MlmeStatus), 
 * MlmeReassociate.confirm(MlmeStatus), 
 * MlmePowermgt.confirm(MlmeStatus), 
 * MlmeReset.confirm(MlmeStatus), 
 * MlmeScan.confirm(BssDscrSet,MlmeStatus), 
 * MlmeStart.confirm(MlmeStatus),
 * ...
 */
typedef uint16_t m80211_mgmt_status_t;
/* Status codes (IEEE 802.11-2007, 7.3.1.9, Table 7-23) */
#define M80211_MGMT_STATUS_SUCCESSFUL                                 0
#define M80211_MGMT_STATUS_UNSPECIFIED_FAILURE                        1
#define M80211_MGMT_STATUS_CAPABILITY_NOT_SUPPORTED                  10
#define M80211_MGMT_STATUS_REASSOC_DENIED                            11 /* REASSOC_NO_ASSOC */
#define M80211_MGMT_STATUS_ASSOC_DENIED                              12
#define M80211_MGMT_STATUS_AUTH_ALG_NOT_SUPPORTED                    13
#define M80211_MGMT_STATUS_WRONG_SEQUENCE_NUMBER                     14 /* UNKNOWN_AUTH_TRANSACTION */
#define M80211_MGMT_STATUS_AUTH_REJECTED_CHALLENGE                   15
#define M80211_MGMT_STATUS_AUTH_REJECTED_TIMEOUT                     16
#define M80211_MGMT_STATUS_ASSOC_DENIED_TO_MANY_STA                  17
#define M80211_MGMT_STATUS_ASSOC_DENIED_UNSUPPORTED_RATE             18
/* IEEE 802.11b */
#define M80211_MGMT_STATUS_ASSOC_DENIED_NOSHORT                      19
#define M80211_MGMT_STATUS_ASSOC_DENIED_NOPBCC                       20
#define M80211_MGMT_STATUS_ASSOC_DENIED_NOAGILITY                    21
/* IEEE 802.11h */
#define M80211_MGMT_STATUS_SPEC_MGMT_REQUIRED                        22
#define M80211_MGMT_STATUS_PWR_CAPABILITY_NOT_VALID                  23
#define M80211_MGMT_STATUS_SUPPORTED_CHANNEL_NOT_VALID               24
/* IEEE 802.11g */
#define M80211_MGMT_STATUS_ASSOC_DENIED_NO_SHORT_SLOT_TIME           25
#define M80211_MGMT_STATUS_ASSOC_DENIED_NO_ER_PBCC                   26
#define M80211_MGMT_STATUS_ASSOC_DENIED_NO_DSSS_OFDM                 27
/* IEEE 802.11w */
#define M80211_MGMT_STATUS_ASSOC_REJECTED_TEMPORARILY                30
#define M80211_MGMT_STATUS_ROBUST_MGMT_FRAME_POLICY_VIOLATION        31
/* IEEE 802.11i */
#define M80211_MGMT_STATUS_INVALID_INFORMATION_ELEMENT               40
#define M80211_MGMT_STATUS_INVALID_GROUP_CIPHER                      41
#define M80211_MGMT_STATUS_INVALID_PAIRWISE_CIPHER                   42
#define M80211_MGMT_STATUS_INVALID_AKMP                              43
#define M80211_MGMT_STATUS_UNSUPPORTED_RSN_IE_VERSION                44
#define M80211_MGMT_STATUS_INVALID_RSN_IE_CAPAB                      45
#define M80211_MGMT_STATUS_CIPHER_REJECTED_PER_POLICY                46
#define M80211_MGMT_STATUS_TS_NOT_CREATED                            47
#define M80211_MGMT_STATUS_DIRECT_LINK_NOT_ALLOWED                   48
#define M80211_MGMT_STATUS_DEST_STA_NOT_PRESENT                      49
#define M80211_MGMT_STATUS_DEST_STA_NOT_QOS_STA                      50
#define M80211_MGMT_STATUS_ASSOC_DENIED_LISTEN_INT_TOO_LARGE         51
/* IEEE 802.11r */
#define M80211_MGMT_STATUS_INVALID_FT_ACTION_FRAME_COUNT             52
#define M80211_MGMT_STATUS_INVALID_PMKID                             53
#define M80211_MGMT_STATUS_INVALID_MDIE                              54
#define M80211_MGMT_STATUS_INVALID_FTIE                              55


/*
 * Management ReasonCodes
 *
 * MlmeDeauthenticate.indication(MacAddr,ReasonCode),
 * MlmeDeauthenticate.indication(MacAddr,ReasonCode),
 * MlmeDisassociate.indication(MacAddr,ReasonCode),
 * ...
 * MlmeDeauthenticate.request(MacAddr,ReasonCode),
 * MlmeDisassociate.request(MacAddr,ReasonCode),
 * ...
*/
typedef uint16_t m80211_mgmt_reason_t;
#define M80211_MGMT_REASON_RESERVED                                   0
/* Reason codes (IEEE 802.11-2007, 7.3.1.7, Table 7-22) */
#define M80211_MGMT_REASON_UNSPECIFIED_REASON                         1
#define M80211_MGMT_REASON_PREV_AUTHENTICATION_NO_LONGER_VALID        2
#define M80211_MGMT_REASON_DEAUTH_LEAVING                             3
#define M80211_MGMT_REASON_DISASSOC_DUE_TO_INACTIVITY                 4
#define M80211_MGMT_REASON_DISASSOC_AP_BUSY                           5
#define M80211_MGMT_REASON_CLASS2_FRAME_FROM_NONAUTHENTICATED_STATION 6
#define M80211_MGMT_REASON_CLASS3_FRAME_FROM_NONASSOCIATED_STATION    7
#define M80211_MGMT_REASON_DISASSOC_STA_HAS_LEFT                      8
#define M80211_MGMT_REASON_STA_REQ_ASSOC_WITHOUT_AUTH                 9
/* IEEE 802.11h */
#define M80211_MGMT_REASON_PWR_CAPABILITY_NOT_VALID                  10
#define M80211_MGMT_REASON_SUPPORTED_CHANNEL_NOT_VALID               11
/* IEEE 802.11i */
#define M80211_MGMT_REASON_INVALID_IE                                13
#define M80211_MGMT_REASON_MICHAEL_MIC_FAILURE                       14
#define M80211_MGMT_REASON_4WAY_HANDSHAKE_TIMEOUT                    15
#define M80211_MGMT_REASON_GROUP_KEY_UPDATE_TIMEOUT                  16
#define M80211_MGMT_REASON_IE_IN_4WAY_DIFFERS                        17
#define M80211_MGMT_REASON_GROUP_CIPHER_NOT_VALID                    18
#define M80211_MGMT_REASON_PAIRWISE_CIPHER_NOT_VALID                 19
#define M80211_MGMT_REASON_AKMP_NOT_VALID                            20
#define M80211_MGMT_REASON_UNSUPPORTED_RSN_IE_VERSION                21
#define M80211_MGMT_REASON_INVALID_RSN_IE_CAPAB                      22
#define M80211_MGMT_REASON_IEEE_802_1X_AUTH_FAILED                   23
#define M80211_MGMT_REASON_CIPHER_SUITE_REJECTED                     24

/*
 * MLME result codes 
 *
 * mlme_status/mlme_result can be internal codes from the 
 * x_mac. We will use code 2-9 for internal result codes 
 * as these are reserced in the 802.11 standard.
 * (IEEE 802.11-2007, 7.3.1.9, Table 7-23)
 */
typedef uint16_t m80211_mlme_result_t;
#define M80211_MLME_RESULT_SUCCESS                   0 /* same as M80211_MGMT_STATUS_SUCCESSFUL */
#define M80211_MLME_RESULT_INVALID_PARAM             1 /* same as M80211_MGMT_STATUS_UNSPECIFIED_FAILURE */
#define M80211_MLME_RESULT_TIMEOUT                   2
#define M80211_MLME_RESULT_TOO_MANY_REQ              3
#define M80211_MLME_RESULT_REFUSED                   4
#define M80211_MLME_RESULT_NO_STA                    5
#define M80211_MLME_RESULT_NOT_AUTHENTICATED         6
#define M80211_MLME_RESULT_ALREADY_STARTED_OR_JOINED 7
#define M80211_MLME_RESULT_NOT_SUPPORTED             8
#define M80211_MLME_RESULT_ALL_SCANJOBS_COMPLETE     9
/* see M80211_MGMT_STATUS_... for more codes sent from the AP */


/************************************************/
/* Information Element (IE) related definitions */
/************************************************/
typedef uint8_t m80211_ie_id_t;
typedef uint8_t m80211_ie_len_t;

/* IE id's */
#define M80211_IE_ID_SSID                       0
#define M80211_IE_ID_SUPPORTED_RATES            1
#define M80211_IE_ID_FH_PAR_SET                 2
#define M80211_IE_ID_DS_PAR_SET                 3
#define M80211_IE_ID_CF_PAR_SET                 4
#define M80211_IE_ID_TIM                        5
#define M80211_IE_ID_IBSS_PAR_SET               6
#define M80211_IE_ID_COUNTRY                    7
#define M80211_IE_ID_HOPPING_PATTERN_PAR        8
#define M80211_IE_ID_HOPPING_PATTERN_TABLE      9
#define M80211_IE_ID_REQUEST                   10
#define M80211_IE_ID_QBSS_LOAD                 11 /* in IEEE 802.11e */
#define M80211_IE_ID_EDCA_PARAMETER_SET        12 /* in IEEE 802.11e */
#define M80211_IE_ID_CHALLENGE_TEXT            16
#define M80211_IE_ID_POWER_CONSTRAINT          32
#define M80211_IE_ID_POWER_CAPABILITY          33
#define M80211_IE_ID_TPC_REQUEST               34
#define M80211_IE_ID_TPC_REPORT                35
#define M80211_IE_ID_SUPPORTED_CHANNELS        36
#define M80211_IE_ID_CSA                       37
#define M80211_IE_ID_MEASUREMENT_REQUEST       38
#define M80211_IE_ID_MEASUREMENT_REPORT        39
#define M80211_IE_ID_QUIET                     40
#define M80211_IE_ID_IBSS_DFS                  41
#define M80211_IE_ID_ERP                       42
#define M80211_IE_ID_HT_CAPABILITIES           45 /* in IEEE 802.11n */
#define M80211_IE_ID_QOS_CAPABILITY            46 /* in IEEE 802.11e */
#define M80211_IE_ID_RSN                       48
#define M80211_IE_ID_EXTENDED_SUPPORTED_RATES  50
#define M80211_IE_ID_AP_CHANNEL_REPORT         51
#define M80211_IE_ID_NEIGHBOR_REPORT           52
#define M80211_IE_ID_RCPI                      53
#define M80211_IE_ID_HT_OPERATION              61
#define M80211_IE_ID_BSS_AVERAGE_DELAY         63
#define M80211_IE_ID_ANTENNA_INFORMATION       64
#define M80211_IE_ID_RSNI                      65
#define M80211_IE_ID_MPILOT_TRANSMISSION_INFO  66
#define M80211_IE_ID_BSS_AVAIL_ADMISSION_CAPACITY  67
#define M80211_IE_ID_BSS_AC_ACCESS_DELAY       68
#define M80211_IE_ID_VENDOR_SPECIFIC          221 /* in WMM & WPA */
#define M80211_IE_ID_WAPI_VENDOR_SPECIFIC      68 /* in WAPI*/
#if (DE_CCX == CFG_INCLUDED)
#define M80211_IE_ID_CISCO_VENDOR_SPECIFIC    150 /* in CCX*/
#define M80211_IE_ID_CISCO_ADJ_VENDOR_SPECIFIC 155 /* in CCX ADJ*/
#define M80211_IE_ID_CISCO_CCKM_VENDOR_SPECIFIC 156 /* in CCX REASSOC*/
#endif
#define M80211_IE_ID_NOT_USED                 255

/* IE vendor specific values for OUI 00:50:f2 */
#define WPA_IE_OUI_TYPE                            1
#define WMM_IE_OUI_TYPE                            2
#define WPS_IE_OUI_TYPE                            4
#define WAPI_IE_OUI_TYPE                           2 /* FIXME: This is actually the AKM Suite */
#define WAPI_VERSION_SIZE                          2
#if (DE_CCX == CFG_INCLUDED)
#define CCX_IE_OUI_TYPE                            3
#define CCX_RM_IE_OUI_TYPE                         1
#define CCX_CPL_IE_OUI_TYPE                        0
#endif
#define NOT_VENDOR_SPECIFIC                        0
#define M80211_IE_ID_VENDOR_SPECIFIC_HDR_SIZE      4

/* IE WMM values */
#define WMM_IE_OUI_SUBTYPE_INFORMATION             0
#define WMM_IE_OUI_SUBTYPE_PARAMETERS              1
#define WMM_IE_OUI_SUBTYPE_TSPEC                   2
#define WMM_IE_PROTOCOL_VERSION                    1 


/* IE attribute field length definitions */
#define M80211_IE_MAX_LEN                          253
#define M80211_IE_MAX_NUM_PAIWISE_SUITE_SELECTORS  6
#define M80211_IE_MAX_NUM_AKM_SUITE_SELECTORS      4
#define M80211_IE_MAX_LENGTH_SSID                  32
#define M80211_IE_MAX_LENGTH_CHALLENGE_TEXT        253
#define M80211_IE_MAX_LENGTH_VIRTUAL_BITMAP        251
#define M80211_IE_MAX_LENGTH_SUPPORTED_RATES       8
#define M80211_IE_MAX_LENGTH_EXT_SUPPORTED_RATES   16
#define M80211_IE_MAX_LENGTH_SUITE_SELECTOR        4
#define M80211_IE_MAX_NUM_COUNTRY_CHANNELS         14
#define M80211_IE_LEN_COUNTRY_STRING               3
#define M80211_IE_CHANNEL_INFO_TRIPLET_SIZE        3
#define M80211_IE_LEN_WEP_CHALLENGE_TEXT           128
#define M80211_IE_LEN_RSN_POOL                     (256-16)
#define M80211_IE_WMM_PARAM_LENGTH                 24

#define M80211_IE_LEN_PMKID                        16

/* IE attribute field definitions */
#define M80211_IE_BITMASK_ERP_NONE_ERP_PRESENT     0x01
#define M80211_IE_BITMASK_ERP_USE_PROTECTION       0x02
#define M80211_IE_BITMASK_ERP_BARKER_PREAMBLE_MODE 0x04

/* Security IE OUIs */
#define M80211_RSN_OUI "\x00\x0F\xAC"
#define M80211_WPA_OUI "\x00\x50\xF2"
#define M80211_WPS_OUI "\x00\x50\xF2"
#define WAPI_OUI       "\x00\x14\x72"
#if (DE_CCX == CFG_INCLUDED)
#define M80211_CCX_OUI "\x00\x40\x96"
#endif

typedef struct
{
   uint8_t     count;
   uint8_t     period;
}m80211_tbtt_timing_t;

typedef struct
{
   char octet[3];
}m80211_oui_id_t;

typedef uint8_t m80211_oui_type_t;

typedef uint16_t m80211_rsn_version_t;
#define M80211_RSN_VERSION 1
#define M80211_WAPI_VERSION 1

typedef uint8_t m80211_cipher_suite_t;
#define M80211_CIPHER_SUITE_GROUP    0
#define M80211_CIPHER_SUITE_WEP40    1
#define M80211_CIPHER_SUITE_TKIP     2
#define M80211_CIPHER_SUITE_WPI      3
#define M80211_CIPHER_SUITE_CCMP     4
#define M80211_CIPHER_SUITE_WEP104   5
#define M80211_CIPHER_SUITE_WEP      6
#define M80211_CIPHER_SUITE_NONE     7

typedef uint8_t m80211_akm_suite_t;
#if (DE_CCX == CFG_INCLUDED)
#define M80211_AKM_SUITE_802X_CCKM   0
#endif
#define M80211_AKM_SUITE_802X_PMKSA  1
#define M80211_AKM_SUITE_PSK         2

typedef uint8_t m80211_protect_type_t;
#define M80211_PROTECT_TYPE_NONE  0
#define M80211_PROTECT_TYPE_RX    1
#define M80211_PROTECT_TYPE_TX    2
#define M80211_PROTECT_TYPE_RX_TX 3
 
typedef uint16_t m80211_rsn_capabilities_t;
#define M80211_RSN_CAPABILITY_PREAUTHENTICATION         0x0001
#define M80211_RSN_CAPABILITY_NO_PAIRWISE               0x0002
#define M80211_RSN_CAPABILITY_PTKSA_1_REPLAY_COUNTER    0x0000
#define M80211_RSN_CAPABILITY_PTKSA_2_REPLAY_COUNTER    0x0004
#define M80211_RSN_CAPABILITY_PTKSA_4_REPLAY_COUNTER    0x0008
#define M80211_RSN_CAPABILITY_PTKSA_16_REPLAY_COUNTERS  0x000C
#define M80211_RSN_CAPABILITY_GTKSA_1_REPLAY_COUNTER    0x0000
#define M80211_RSN_CAPABILITY_GTKSA_2_REPLAY_COUNTER    0x0010
#define M80211_RSN_CAPABILITY_GTKSA_4_REPLAY_COUNTER    0x0020
#define M80211_RSN_CAPABILITY_GTKSA_16_REPLAY_COUNTERS  0x0030
#define M80211_RSN_CAPABILITY_PTKSA_REPLAY_COUNTER_MASK 0x000C
#define M80211_RSN_CAPABILITY_GTKSA_REPLAY_COUNTER_MASK 0x0030

typedef uint16_t m80211_ht_capabilities_t;
#define M80211_HT_CAPABILITY_LDPC                      0x0001
#define M80211_HT_CAPABILITY_SUPP_CH_WIDTH_SET         0x0002
#define M80211_HT_CAPABILITY_SM_POWER_SAVE_MASK        0x000C
#define M80211_HT_CAPABILITY_HT_GREENFIELD             0x0010
#define M80211_HT_CAPABILITY_SHORT_GI_20MHZ            0x0020
#define M80211_HT_CAPABILITY_SHORT_GI_40MHZ            0x0040
#define M80211_HT_CAPABILITY_TX_STBC                   0x0080
#define M80211_HT_CAPABILITY_RX_STBC_MASK              0x0300
#define M80211_HT_CAPABILITY_RX_STBC_UNSUPPORTED       0x0000
#define M80211_HT_CAPABILITY_RX_STBC_ONE_STREAM        0x0100
#define M80211_HT_CAPABILITY_RX_STBC_TWO_STREAMS       0x0200
#define M80211_HT_CAPABILITY_RX_STBC_THREE_STREAMS     0x0300
#define M80211_HT_CAPABILITY_HT_DELAYED_BLOCK_ACK      0x0400
#define M80211_HT_CAPABILITY_MAX_AMPDU_LENGTH          0x0800
#define M80211_HT_CAPABILITY_DSS_CCK_MODE_40MHZ        0x1000
#define M80211_HT_CAPABILITY_40MHZ_INTOLERANT          0x4000
#define M80211_HT_CAPABILITY_LSIG_TXOP_PROTECT_SUPPORT 0x8000

typedef uint8_t m80211_measurement_types_t;
#define M80211_MEASUREMENT_BASIC_REQ                  0
#define M80211_MEASUREMENT_CCA_REQ                    1
#define M80211_MEASUREMENT_RPI_HISTOGRAM_REQ          2
#define M80211_MEASUREMENT_CHANNEL_LOAD_REQ           3
#define M80211_MEASUREMENT_NOISE_HISTOGRAM_REQ        4
#define M80211_MEASUREMENT_BEACON_REQ                 5
#define M80211_MEASUREMENT_FRAME_REQ                  6
#define M80211_MEASUREMENT_STA_STATISTICS_REQ         7
#define M80211_MEASUREMENT_LCI_REQ                    8
#define M80211_MEASUREMENT_TRANSMIT_STREAM_REQ        9
#define M80211_MEASUREMENT_PAUSE_REQ                  255

#define M80211_MEASUREMENT_BASIC_REP                  0
#define M80211_MEASUREMENT_CCA_REP                    1
#define M80211_MEASUREMENT_RPI_HISTOGRAM_REP          2
#define M80211_MEASUREMENT_CHANNEL_LOAD_REP           3
#define M80211_MEASUREMENT_NOISE_HISTOGRAM_REP        4
#define M80211_MEASUREMENT_BEACON_REP                 5
#define M80211_MEASUREMENT_FRAME_REP                  6
#define M80211_MEASUREMENT_STA_STATISTICS_REP         7
#define M80211_MEASUREMENT_LCI_REP                    8
#define M80211_MEASUREMENT_TRANSMIT_STREAM_REP        9
#define M80211_MEASUREMENT_PAUSE_REP                  255


typedef uint8_t m80211_measurement_mode_t;
#define M80211_MEASUREMENT_MODE_PARALLEL                0x1
#define M80211_MEASUREMENT_MODE_ENABLE                  0x2
#define M80211_MEASUREMENT_MODE_REQUEST                 0x4
#define M80211_MEASUREMENT_MODE_REPORT                  0x8
#define M80211_MEASUREMENT_MODE_DURATION_MANDATORY      0x10

typedef uint8_t m80211_measurement_report_mode_t;
#define M80211_MEASUREMENT_REPORT_MODE_SUCCESS          0x0
#define M80211_MEASUREMENT_REPORT_MODE_LATE             0x1
#define M80211_MEASUREMENT_REPORT_MODE_INCAPABLE        0x2
#define M80211_MEASUREMENT_REPORT_MODE_REFUSED          0x4


/********************************************************/
/* Action category / details definitions                */
/********************************************************/
typedef uint8_t m80211_action_category_t;
#define M80211_ACTION_CATEGORY_SPECTRUM_MEASUREMENT      0
#define M80211_ACTION_CATEGORY_RADIO_MEASUREMENT         5

typedef uint8_t m80211_spectrum_measurement_action_detail_t;
#define M80211_SPECTRUM_MEASUREMENT_ACTION_DETAIL_MEASUREMENT_REQUEST         0
#define M80211_SPECTRUM_MEASUREMENT_ACTION_DETAIL_MEASUREMENT_REPORT          1
#define M80211_SPECTRUM_MEASUREMENT_ACTION_DETAIL_TPC_REQUEST                 2
#define M80211_SPECTRUM_MEASUREMENT_ACTION_DETAIL_TPC_REPORT                  3
#define M80211_SPECTRUM_MEASUREMENT_ACTION_DETAIL_CSA                         4

typedef uint8_t m80211_radio_measurement_action_detail_t;
#define M80211_RADIO_MEASUREMENT_ACTION_DETAIL_RADIO_MEASUREMENT_REQUEST      0
#define M80211_RADIO_MEASUREMENT_ACTION_DETAIL_RADIO_MEASUREMENT_REPORT       1
#define M80211_RADIO_MEASUREMENT_ACTION_DETAIL_LINK_MEASUREMENT_REQUEST       2
#define M80211_RADIO_MEASUREMENT_ACTION_DETAIL_LINK_MEASUREMENT_REPORT        3
#define M80211_RADIO_MEASUREMENT_ACTION_DETAIL_NEIGHBOR_MEASUREMENT_REQUEST   4
#define M80211_RADIO_MEASUREMENT_ACTION_DETAIL_NEIGHBOR_MEASUREMENT_REPORT    5

typedef uint8_t m80211_beacon_mode_t;
#define M80211_BEACON_PASSIVE          0
#define M80211_BEACON_ACTIVE           1
#define M80211_BEACON_TABLE            2

typedef uint8_t m80211_beacon_report_condition_t;
#define M80211_BEACON_REPORT_CONDITION_AFTER_EACH                    0
#define M80211_BEACON_REPORT_CONDITION_RCPI_GT_ABS_THR               1
#define M80211_BEACON_REPORT_CONDITION_RCPI_LT_ABS_THR               2
#define M80211_BEACON_REPORT_CONDITION_RSNI_GT_ABS_THR               3
#define M80211_BEACON_REPORT_CONDITION_RSNI_LT_ABS_THR               4
#define M80211_BEACON_REPORT_CONDITION_RCPI_GT_OFF_THR_REF_AP        5
#define M80211_BEACON_REPORT_CONDITION_RCPI_LT_OFF_THR_REF_AP        6
#define M80211_BEACON_REPORT_CONDITION_RSNI_GT_OFF_THR_REF_AP        7
#define M80211_BEACON_REPORT_CONDITION_RSNI_LT_OFF_THR_REF_AP        8
#define M80211_BEACON_REPORT_CONDITION_RCPI_IN_RANGE_OFF_THR_REF_AP  9
#define M80211_BEACON_REPORT_CONDITION_RSNI_IN_RANGE_OFF_THR_REF_AP  10

typedef int8_t m80211_threshold_offset;

/* Location Configuration Indication request types */
typedef uint8_t m80211_location_subject_t;
typedef uint8_t m80211_latitude_requested_resolution_t;
typedef uint8_t m80211_longitude_requested_resolution_t;
typedef uint8_t m80211_altitude_requested_resolution_t;
typedef uint8_t m80211_azimuth_request_t;

typedef uint8_t*        ie_ref_t;

typedef struct 
{
   m80211_oui_id_t   id;
   m80211_oui_type_t type;
}m80211_oui_t;

typedef struct 
{
   m80211_oui_id_t       id;
   m80211_cipher_suite_t type;
}m80211_cipher_suite_selector_t;

typedef struct 
{
   m80211_oui_id_t    id;
   m80211_akm_suite_t type;
}m80211_akm_suite_selector_t;

typedef struct 
{
   uint8_t octet[M80211_IE_LEN_PMKID];
}m80211_pmkid_selector_t;


typedef union GENERATE_WRAPPER_FUNCTIONS(802.11_MAC)
{
   m80211_cipher_suite_selector_t   cipher;
   m80211_akm_suite_selector_t      akm;
   m80211_pmkid_selector_t          pmkid;
} suite_selector_t;

typedef struct 
{
   char string[M80211_IE_LEN_COUNTRY_STRING];
}m80211_country_string_t;


typedef struct
{
   uint8_t  first_channel;
   uint8_t  num_channels;
   int8_t   max_tx_power;
}m80211_country_channels_t;

typedef struct 
{
   int8_t max_dBm;
}m80211_max_regulatory_power_t;

typedef struct 
{
   int8_t tx_dBm;    /* Max allowed Tx power on antenna connector. <= Max regulatory power */
}m80211_max_tx_power_t;

typedef struct 
{
   int8_t tx_dBm;    /* Tx power used on antenna connector. */
}m80211_tx_power_used_t;

typedef struct 
{
   int8_t tx_dBm;    /* Transceiver noise floor on receiver antenna connector. */
}m80211_trx_noise_floor_t;

/* IE structure format definitions */
typedef struct
{
   m80211_ie_id_t  id;
   m80211_ie_len_t len;
}m80211_ie_hdr_t;

typedef struct
{
   m80211_ie_hdr_t hdr;
   char            first_octet;
}m80211_ie_format_t;

typedef struct 
{
   m80211_ie_hdr_t hdr;
   char            ssid[M80211_IE_MAX_LENGTH_SSID]; 
}m80211_ie_ssid_t;


typedef struct 
{
   m80211_ie_hdr_t hdr;
   m80211_std_rate_encoding_t rates[M80211_IE_MAX_LENGTH_SUPPORTED_RATES]; 
}m80211_ie_supported_rates_t;

typedef struct 
{
   m80211_ie_hdr_t hdr;
   m80211_std_rate_encoding_t rates[M80211_IE_MAX_LENGTH_EXT_SUPPORTED_RATES]; 
}m80211_ie_ext_supported_rates_t;

typedef struct 
{
   m80211_ie_hdr_t hdr;
   uint16_t        dwell_time;
   uint8_t         hop_set;
   uint8_t         hop_pattern;
   uint8_t         hop_index;
}m80211_ie_fh_par_set_t;

typedef struct 
{
   m80211_ie_hdr_t hdr;
   uint8_t         channel; 
}m80211_ie_ds_par_set_t;

typedef struct
{
   m80211_ie_hdr_t      hdr;
   m80211_tbtt_timing_t cf_timing;
   m80211_tu16_t        max_duration;
   m80211_tu16_t        duration_remaining;
}m80211_ie_cf_par_set_t;

typedef struct
{
   m80211_tbtt_timing_t dtim_timing;
   uint8_t              bitmap_control;
}m80211_ie_tim_fixed_part_t;

typedef struct
{
   m80211_ie_hdr_t      hdr;
   m80211_tbtt_timing_t dtim_timing;
   uint8_t              bitmap_control;
   char*                bitmap;
}m80211_ie_tim_t;

typedef struct
{
   m80211_ie_hdr_t hdr;
   uint16_t        atim_window;
}m80211_ie_ibss_par_set_t;

typedef struct
{
   m80211_ie_hdr_t            hdr;
   m80211_country_string_t    country_string;
   m80211_country_channels_t* channel_info;
}m80211_ie_country_t;

typedef struct
{
   m80211_ie_hdr_t           hdr;
    uint16_t        station_cnt;
    uint8_t         channel_util;
    uint16_t        avail_adm_capa;
}m80211_ie_qbss_load_t;

typedef struct
{
   m80211_ie_hdr_t           hdr;
   m80211_ie_id_t            first_requested_ie;
}m80211_ie_request_t;

typedef struct 
{
   m80211_ie_hdr_t hdr;
   uint8_t         challenge_text[M80211_IE_MAX_LENGTH_CHALLENGE_TEXT]; 
}m80211_ie_challenge_text_t;


typedef struct 
{
   m80211_ie_hdr_t hdr;
   uint8_t         info;
}m80211_ie_erp_t;

typedef struct 
{
   m80211_ie_hdr_t                     hdr;
   uint16_t                            version;
   m80211_cipher_suite_selector_t      group_cipher_suite;   
   uint16_t                            pairwise_cipher_suite_count;
   uint16_t                            akm_suite_count;
   uint16_t                            rsn_capabilities;
   uint16_t                            pmkid_count;
   /* The rsn_pool must be on a 32bit aligned adress. */
   char*                               rsn_pool;       
}m80211_ie_rsn_parameter_set_t;

typedef struct
{
   m80211_ie_hdr_t      hdr;
   uint8_t              prime_radix;
   uint8_t              number_of_channels;
}m80211_ie_fh_parameters_t;

typedef struct
{
   m80211_ie_hdr_t      hdr;
   uint8_t              flag;
   uint8_t              number_of_sets;
   uint8_t              modulus;
   uint8_t              offset;
   uint8_t              random_table[251];   /* max IE length -4 */
}m80211_ie_fh_pattern_table_t;

typedef struct
{
   m80211_ie_hdr_t      hdr;
   uint8_t              local_power_constraint;  /* dB relative to 1 mW */
}m80211_ie_power_constraint_t;

typedef struct
{
   m80211_ie_hdr_t      hdr;
   uint8_t              channel_switch_mode;
   uint8_t              new_channel;
   uint8_t              channel_switch_count;
}m80211_ie_channel_switch_announcement_t;

typedef struct
{
   m80211_ie_hdr_t      hdr;
   uint8_t              quiet_count;
   uint8_t              quiet_period;
   uint16_t             quiet_duration;
   uint16_t             quiet_offset;
}m80211_ie_quiet_t;

typedef struct
{
   uint8_t  channel_no;
   uint8_t  map;
}channel_map_t;

typedef struct
{
   m80211_ie_hdr_t      hdr;
   m80211_mac_addr_t    dfs_owner;
   uint8_t              dfs_recovery_interval;
   channel_map_t        chmap[14];     /* M80211_CHANNEL_LIST_MAX_LENGTH */
}m80211_ie_ibss_dfs_t;

typedef struct
{
   m80211_ie_hdr_t      hdr;
   uint8_t              transmit_power;
   uint8_t              link_margin;
}m80211_ie_tpc_report_t;

typedef struct
{
   m80211_ie_hdr_t      hdr;
   uint16_t             station_count;
   uint8_t              channel_utilization;
   uint16_t             avail_admission_capacity;
}m80211_ie_bss_load_t;

typedef uint8_t   aci_aifsn_t;
typedef uint8_t   ecwin_min_max_t;
typedef struct
{
   aci_aifsn_t          aci_aifsn;        /* Bit field: b0-b3 AIFSN, b4 ACM, b5-b6 ACI */
   ecwin_min_max_t      ecwin_min_max;    /* 2's exponent: b0-b3 min, b4-b7 max */
   uint16_t             txop_limit;
}ac_param_t;

typedef struct
{
   m80211_ie_hdr_t      hdr;
   uint8_t              qos_info;
   uint8_t              reserved;
   ac_param_t           ac_be;
   ac_param_t           ac_bk;
   ac_param_t           ac_vi;
   ac_param_t           ac_vo;   
}m80211_ie_edca_parameter_set_t;

typedef struct
{
   m80211_ie_hdr_t      hdr;
   uint8_t              qos_info;
}m80211_ie_qos_capability_t;

typedef struct
{
   m80211_ie_hdr_t      hdr;
   uint8_t              regulatory_class;
   uint8_t              channelList[14];     /* M80211_CHANNEL_LIST_MAX_LENGTH */
}m80211_ie_ap_channel_report_t;

typedef struct
{
   m80211_ie_hdr_t      hdr;
   uint8_t              ap_delay;
}m80211_ie_bss_average_access_delay_t;

typedef struct
{
   m80211_ie_hdr_t      hdr;
   uint8_t              antenna_id;
}m80211_ie_antenna_information_t;

typedef struct
{
   m80211_ie_hdr_t      hdr;
   uint16_t             mpilot_interval;
}m80211_ie_mpilot_transmission_info_t;

typedef struct
{
   m80211_ie_hdr_t      hdr;
   uint16_t             avail_bitmask;
   uint16_t             avail_list;       /* no of 32 us segments available for 1 sec  */
}m80211_ie_bss_avail_admission_capacity_t;

#define M80211_SIZEOF_HT_MCS_BITMAP 10

typedef struct
{
   uint8_t  rx_mcs_bitmap[M80211_SIZEOF_HT_MCS_BITMAP];
   uint8_t  rx_highest_supp_data_rate_low_byte;
   uint8_t  rx_highest_supp_data_rate_high_byte;
   uint8_t  tx_mcs_flags;
   uint8_t  reserved[3];
}m80211_supported_mcs_set_t;

typedef struct
{
   m80211_ie_hdr_t            hdr;
   uint8_t                    ht_capabilities_info[2];
   uint8_t                    a_mpdu_parameters;
   m80211_supported_mcs_set_t supported_mcs_set;
   uint8_t                    ht_extended_capabilities[2];
   uint8_t                    transmit_beamforming_capabilities[4];
   uint8_t                    asel_capabilities;
}m80211_ie_ht_capabilities_t;

typedef struct
{
   m80211_ie_hdr_t      hdr;
   uint8_t              primary_channel;
   uint8_t              bitfield[5];
   uint8_t              basic_mcs_set[16];
}m80211_ie_ht_operation_t;

typedef struct
{
   uint8_t     best_effort;
   uint8_t     background;
   uint8_t     video;
   uint8_t     vioce;
}access_category_delay_t;
typedef struct
{
   m80211_ie_hdr_t              hdr;
   access_category_delay_t      average_delay;
   }m80211_ie_bss_ac_access_delay_t;

typedef struct
{
   m80211_action_category_t                  category;
   m80211_radio_measurement_action_detail_t  action;
   uint8_t                                   dialog_token;
   uint8_t                                   filler;  /* ifdef M11KSIMTEST */
   uint16_t                                  number_of_rep;
}meas_req_hdr_t;

typedef struct
{
   m80211_action_category_t                  category;
   m80211_radio_measurement_action_detail_t  action;
   uint8_t                                   dialog_token;
}meas_rep_hdr_t;

typedef struct
{
   uint8_t  regulatory_class;
   uint8_t  channel_number;
   uint16_t random_interval;
   uint16_t measurement_duration;
}radio_meas_req_hdr_t;

typedef struct
{
   uint8_t  regulatory_class;
   uint8_t  channel_number;
   uint16_t random_interval;
   uint16_t measurement_duration;
}radio_meas_rep_hdr_t;


typedef struct
{
   radio_meas_req_hdr_t  hdr;
   m80211_location_subject_t loc_subj;
   m80211_latitude_requested_resolution_t lat_req;
   m80211_longitude_requested_resolution_t long_req;
   m80211_altitude_requested_resolution_t alt_req;
   m80211_azimuth_request_t azi_req;
}lci_req_t;


typedef struct
{
   radio_meas_req_hdr_t  hdr;
}channel_load_req_t;

typedef struct
{
   radio_meas_rep_hdr_t  hdr;
}channel_load_rep_t;

typedef struct
{
   radio_meas_req_hdr_t  hdr;
}noise_histogram_req_t;

typedef struct
{
   radio_meas_rep_hdr_t  hdr;
}noise_histogram_rep_t;

typedef struct
{
   radio_meas_req_hdr_t  hdr;
   m80211_beacon_mode_t  mode;
   m80211_mac_addr_t bssid;
   m80211_beacon_report_condition_t report_condition;
   m80211_threshold_offset thr_off;
   m80211_ie_ssid_t ssid;
}beacon_req_t;

typedef struct
{
   radio_meas_rep_hdr_t  hdr;
   m80211_beacon_mode_t  mode;
   m80211_mac_addr_t bssid;
   m80211_beacon_report_condition_t report_condition;
   m80211_threshold_offset thr_off;
   m80211_ie_ssid_t ssid;
}beacon_rep_t;

typedef union 
{
   channel_load_req_t       channel_load_req;
   noise_histogram_req_t    noise_histogram_req;
   beacon_req_t             beacon_req;
   lci_req_t                lci_req;
}m80211k_request;

typedef union 
{
   uint8_t                  buf[200];
   channel_load_rep_t       channel_load_rep;
   noise_histogram_rep_t    noise_histogram_rep;
   beacon_rep_t             beacon_rep;
}m80211k_report;

typedef struct 
{
   m80211_ie_hdr_t            hdr;
   uint8_t                    token;
   m80211_measurement_mode_t  mode;
   m80211_measurement_types_t type;
   m80211k_request            req; 
   uint8_t                    buf[200];   
}m80211_ie_measurement_request_t;

typedef struct 
{
   m80211_ie_hdr_t                  hdr;
   uint8_t                          token;
   m80211_measurement_report_mode_t mode;
   m80211_measurement_types_t       type;
   m80211k_report                   rep; 
}m80211_ie_measurement_report_t;


typedef struct
{
   meas_req_hdr_t                     hdr;
   m80211_ie_measurement_request_t    measurement;
} m80211_radio_measurement_req_t;

typedef struct
{
   meas_rep_hdr_t                     hdr;   
   m80211_ie_measurement_report_t     measurement;
} m80211_radio_measurement_rep_t;

#define M80211_IE_RSN_PARAMETER_SET_GET_PAIRWISE_CIPHER_SELECTORS(_rsn_parameter_set) \
   ((m80211_cipher_suite_selector_t*)&(_rsn_parameter_set)->rsn_pool[0])

#define M80211_IE_RSN_PARAMETER_SET_GET_AKM_SELECTORS(_rsn_parameter_set) \
   ((m80211_akm_suite_selector_t*) \
   ((_rsn_parameter_set)->rsn_pool \
    + (_rsn_parameter_set)->pairwise_cipher_suite_count*sizeof(m80211_cipher_suite_selector_t)))

#define M80211_IE_RSN_PARAMETER_SET_GET_PMKID_SELECTORS(_rsn_parameter_set) \
   ((m80211_pmkid_selector_t*) \
   ((_rsn_parameter_set)->rsn_pool \
    + (_rsn_parameter_set)->pairwise_cipher_suite_count*sizeof(m80211_cipher_suite_selector_t) \
    + (_rsn_parameter_set)->akm_suite_count*sizeof(m80211_akm_suite_selector_t)))

#define M80211_IE_WAPI_PARAMETER_SET_IS_PSK(_wapi_parameter_set) \
   (((_wapi_parameter_set)->wapi_pool[5]) & 0x02 )

#define M80211_IE_WAPI_PARAMETER_SET_IS_CERTIFICATE(_wapi_parameter_set) \
   (((_wapi_parameter_set)->wapi_pool[5]) & 0x01 )


typedef struct
{
   m80211_ie_hdr_t         hdr;    
   uint8_t                 OUI_1;  
   uint8_t                 OUI_2;      
   uint8_t                 OUI_3;      
   uint8_t                 OUI_type;   
}m80211_ie_vendor_specific_hdr_t;

typedef struct
{
  m80211_ie_hdr_t         hdr;
  uint16_t                version;
}m80211_ie_wapi_vendor_specific_hdr_t;

/* bjru: added 2006-03-15 */
typedef struct 
{
   m80211_ie_vendor_specific_hdr_t     hdr;
   m80211_rsn_version_t                version;
   m80211_cipher_suite_selector_t      group_cipher_suite;   
   uint16_t                            pairwise_cipher_suite_count;
   uint16_t                            akm_suite_count;
   uint16_t                            rsn_capabilities;
   char*                               rsn_pool;
}m80211_ie_wpa_parameter_set_t;

#define M80211_IE_WPA_PARAMETER_SET_GET_PAIRWISE_CIPHER_SELECTORS(_wpa_parameter_set) \
   ((m80211_cipher_suite_selector_t*)&(_wpa_parameter_set)->rsn_pool[0])

#define M80211_IE_WPA_PARAMETER_SET_GET_AKM_SELECTORS(_wpa_parameter_set) \
   ((m80211_akm_suite_selector_t*) \
   ((_wpa_parameter_set)->rsn_pool \
    + (_wpa_parameter_set)->pairwise_cipher_suite_count*sizeof(m80211_cipher_suite_selector_t)))


/* 
 * The following data type definition is according to the
 * Wi-Fi Protected Setup Spec Version 1.0h (December 2006)
 *
 * wps_ie.hdr      = { 0xdd, 0xNN, 0x00, 0x50 ,0xf2 ,0x04 };
 * wps_ie.wps_pool = { 0x10 ,0x4a ,0x00 ,0x01 ,0x10 , ... next tlv ... };
 * wps_pool_len = wps_ie.hdr.hdr.len - M80211_IE_ID_VENDOR_SPECIFIC_HDR_SIZE;
 */
typedef struct
{
  m80211_ie_vendor_specific_hdr_t     hdr;
  char*                               wps_pool; /* TLV's */
} m80211_ie_wps_parameter_set_t;

/* The following data type definition is according to the
   WAPI Spec
*/
typedef struct
{
  m80211_ie_wapi_vendor_specific_hdr_t     hdr;
  char*                               wapi_pool;
} m80211_ie_wapi_parameter_set_t;

/* The four following data type definitions are according to the 
   "WMM (including WMM Power Save) Specification Version 1.1"
   
   Note that these are not the same as the corresponding definitions in
   IEEE 802.11e which we don't support (yet) */

typedef struct
{
   m80211_ie_vendor_specific_hdr_t  hdr;
   uint8_t                          OUI_Subtype; /* 0x00 or 0x01 */
} m80211_ie_WMM_header_t;

/* WMM QoS Control Field */
/* See the WMM Spec. chapter 2.1.6, page 7 */
typedef uint16_t WMM_QoS_Control_t;

typedef struct
{
   m80211_ie_WMM_header_t           WMM_hdr;
   uint8_t                          WMM_Protocol_Version; /* == 0x01 */
   uint8_t                          WMM_QoS_Info; /* See fig. 6 and 7, page 9 in the WMM spec. */
}m80211_ie_WMM_information_element_t;

typedef struct GENERATE_WRAPPER_FUNCTIONS(802.11_MAC)
{
   uint8_t                 ACI_ACM_AIFSN; /* ACI, ACM and AIFSN */
   uint8_t                 ECWmin_ECWmax; /* ECW min and ECW max */
   uint16_t                TXOP_Limit;
}AC_parameters_t;

typedef struct
{
   m80211_ie_WMM_header_t           WMM_hdr;
   uint8_t                          WMM_Protocol_Version; /* == 0x01 */
   uint8_t                          WMM_QoS_Info;/* See fig. 6, page 9 in the WMM spec. */
   uint8_t                          reserved;       
   AC_parameters_t                  AC_BE; /* Access Category Best Effort */
   AC_parameters_t                  AC_BK; /* Access Category Background */
   AC_parameters_t                  AC_VI; /* Access Category Video */
   AC_parameters_t                  AC_VO; /* Access Category Voice */
}m80211_ie_WMM_parameter_element_t;

typedef struct 
{
   m80211_ie_hdr_t         hdr;
   uint8_t                 requested_element_id;
}m80211_ie_request_info_t;

typedef struct 
{
   int32_t    count;
   int32_t    acc_size;
   ie_ref_t   buffer_ref;
}m80211_remaining_IEs_t;


#if (DE_CCX == CFG_INCLUDED)
/*
 * AIRONET OUI
 * ccx_ie.hdr      = { 0xdd, 0x05, 0x00, 0x40 ,0x96 ,0x03 };
 * ccx_ie.version =  { 0x04 };
 */
typedef struct
{
  m80211_ie_vendor_specific_hdr_t     hdr;
  uint8_t                             ccx_version;
} m80211_ie_ccx_parameter_set_t;

/*
 * AIRONET RM OUI
 * ccx_ie.hdr      = { 0xdd, 0x05, 0x00, 0x40 ,0x96 ,0x01 };
 * ccx_ie.ccx_rm_status   =  { 0x01, 0x00 };
 */
typedef struct
{
  m80211_ie_vendor_specific_hdr_t     hdr;
  uint16_t                            ccx_rm_status;
} m80211_ie_ccx_rm_parameter_set_t;

/*
 *
 * ccx_ie.hdr      = { 0x96, 0x06, 0x00, 0x40 ,0x96 ,0x00 };
 * ccx_ie.CellPowerLimit =  { x };
 * ccx_ie.Resserved =  { 0 };
 */
typedef struct
{
  m80211_ie_vendor_specific_hdr_t     hdr;
  uint8_t                             cpl;
  uint8_t                             reserved;
} m80211_ie_ccx_cpl_parameter_set_t;

/*
 *
 * ccx_ie.hdr      = { 0xDD, 0x08, 0x00, 0x40 ,0x96 ,0x00 };
 * ccx_ie.tsid =  { 0 };
 * ccx_ie.state =  { 0 };
 * ccx_ie.interval =  { xx };
 */
typedef struct
{
  m80211_ie_vendor_specific_hdr_t     hdr;
  uint8_t                             tsid;
  uint8_t                             state;
  uint16_t                            interval;
} m80211_ie_ccx_tsm_parameter_set_t;

/*
 * The following data type definition is for the ccx AP Assisted Roaming function
 *
 *
 * adj_ie.hdr      = { 0xDD, 0x3D, 0x00, 0x50 ,0xF2 ,0x02 };
 * adj_ie.tspec_pool = {tspec body };
 * adj_pool_len = adj_ie.hdr.hdr.len - M80211_IE_ID_VENDOR_SPECIFIC_HDR_SIZE;
 */
typedef struct
{
  m80211_ie_vendor_specific_hdr_t     hdr;
  char                                mac_address[6];
  uint16_t                            channel;
  uint16_t                            ssid_len;
  char                                ssid[32];
  uint16_t                            secs;
} m80211_ie_ccx_adj_parameter_set_t;

/*
 * The following data type definition is for the ccx AP Reassociation request
 * ccx_ie.hdr      = { 0x9c, 0x18, 0x00, 0x40 ,0x96 ,0x0 };
 */
typedef struct
{
  m80211_ie_vendor_specific_hdr_t     hdr;
  uint8_t                             timestamp[8];
  uint8_t                             request_number[4];
  uint8_t                             MIC[8];

} m80211_ie_ccx_reassoc_req_parameter_set_t;

/*
 * The following data type definition is for the ccx AP Reassociation response
 * ccx_ie.hdr      = { 0x9c, 0xNN, 0x00, 0x40 ,0x96 ,0x0 };
 */
typedef struct
{
  m80211_ie_vendor_specific_hdr_t     hdr;
  uint8_t*                             body_p;
} m80211_ie_ccx_reassoc_rsp_parameter_set_t;

/*
 * The following structs are for ADDTS and DELTS management packets
 */

typedef struct
{
    uint8_t                         category;
    uint8_t                         action_code;
    uint8_t                         dialog_token;
    uint8_t                         status_code;
} m80211_wmm_action_t;

/* WMM TSPEC Element */
typedef struct {
       m80211_ie_WMM_header_t           WMM_hdr;
       uint8_t                          WMM_Protocol_Version;
       char                             TSPEC_body[55];
} m80211_wmm_tspec_ie_t;

typedef struct
{
    uint8_t                                 dialog_token;
    uint8_t                                 status_code;
    m80211_wmm_tspec_ie_t                   wmm_tspec_ie;
}m80211_nrp_mlme_addts_req_body_t;

typedef struct
{
   mac_api_transid_t                    trans_id;
   uint32_t                             action_code;
   m80211_nrp_mlme_addts_req_body_t     body;
}m80211_nrp_mlme_addts_req_t;

typedef struct
{
   mac_api_transid_t trans_id;
   m80211_mlme_result_t value;
   uint16_t             padding;
}m80211_nrp_mlme_addts_cfm_t;

typedef struct
{
   mac_api_transid_t                    trans_id;
   uint32_t                             action_code;
   m80211_nrp_mlme_addts_req_body_t     body;
}m80211_nrp_mlme_addts_ind_t;

typedef struct
{
   mac_api_transid_t                    trans_id;
   uint32_t                             action_code;
   uint8_t                              dialog_token;
   uint8_t                              status_code;
   m80211_wmm_tspec_ie_t                wmm_tspec_ie;
/*
   m80211_ie_WMM_header_t               WMM_hdr;
   uint8_t                              WMM_Protocol_Version;
   uint8_t                              ts_info[3];
   uint8_t                              reason_code;
*/
}m80211_nrp_mlme_delts_req_t;

typedef struct
{
   mac_api_transid_t trans_id;
   uint32_t          result;
   uint32_t          value;
}m80211_nrp_mlme_delts_cfm_t;

typedef struct
{
    mac_api_transid_t trans_id;
    uint32_t          pkt_xmit_delay_total;
    uint16_t          pkt_xmitted_cnt;
    bool_t            init;
    uint8_t           padding;
}m80211_nrp_mlme_fw_stats_cfm_t;

typedef struct
{
    mac_api_transid_t trans_id;
    uint32_t          init;
}m80211_nrp_mlme_fw_stats_req_t;

#endif //DE_CCX

/*******************************************/
/* 802.11 frame format related definitions */
/*******************************************/
typedef struct
{
   uint16_t          frame_ctrl;
   uint16_t          duration_id;
   m80211_mac_addr_t addr1;
}mac_hdr_t;

typedef struct
{
   uint16_t          frame_ctrl;
   uint16_t          duration_id;
   m80211_mac_addr_t addr1;
   m80211_mac_addr_t addr2;
   m80211_mac_addr_t addr3;
   uint16_t          seq_ctrl;
} mac_hdr_data_t;

typedef struct
{
   uint16_t          frame_ctrl;
   uint16_t          duration_id;
   m80211_mac_addr_t addr1;
   m80211_mac_addr_t addr2;
   m80211_mac_addr_t addr3;
   uint16_t          seq_ctrl;
/* m80211_mac_addr_t addr4 is never present! Such frames are discarded on a low level */
   uint16_t          WMM_QoS_Control; /* Only present in WMM QoS Data and QoS Null frames */
}mac_hdr_WMM_QoS_data_t;              /* governed by the frame_ctrl Subtype and Type */

typedef mac_hdr_data_t mac_hdr_mgmt_t;

typedef struct
{
   uint16_t          frame_ctrl;
   uint16_t          duration_id;
   m80211_mac_addr_t addr1;
   m80211_mac_addr_t addr2;
   m80211_mac_addr_t addr3;
   uint16_t          seq_ctrl;
   m80211_mac_addr_t addr4;
   uint16_t          qos;
   char              frame_body[2324];
   uint32_t          fcs;
}mac_max_data_t;

typedef struct
{
   uint16_t          frame_ctrl;
   uint16_t          duration_id;
   m80211_mac_addr_t ra;
   m80211_mac_addr_t ta;
}mac_frame_rts_t;

typedef struct
{
   uint16_t          frame_ctrl;
   uint16_t          duration_id;
   m80211_mac_addr_t ra;
}mac_frame_cts_t;

typedef struct
{
   uint16_t          frame_ctrl;
   uint16_t          aid;
   m80211_mac_addr_t bssid;
   m80211_mac_addr_t ta;
}mac_frame_pspoll_t;

typedef struct
{
   uint64_t        timestamp;
   uint16_t        beacon_interval;
   uint16_t        capabilities;
   m80211_ie_hdr_t first_ie;
}mac_beacon_body_fixed_part_t;

typedef struct
{
   mac_hdr_mgmt_t    header;
   uint64_t          timestamp;
   uint16_t          beacon_interval;
   uint16_t          capabilities;
   m80211_ie_hdr_t   first_ie;
}mac_beacon_probe_fixed_part_t;

typedef struct
{
   uint16_t          frame_ctrl;
   uint16_t          duration_id;
   m80211_mac_addr_t addr1;
   m80211_mac_addr_t addr2;
}mac_ack_required_frame_t;

#define M80211_MAX_80211_FRAMESIZE sizeof(mac_max_data_t)
#define M80211_80211_MAX_DATA_OVERHEAD (sizeof(mac_hdr_WMM_QoS_data_t)+\
                                        sizeof(m80211_tkip_iv_t)+\
                                        sizeof(m80211_wep_icv_t)+\
                                        sizeof(m80211_michael_t))
#define M80211_MAX_ETHERNET_PAYLOAD 1536
#define M80211_MIN_80211_FRAMESIZE  (M80211_MAC_PDU_SIZE_ACK+M80211_MAC_FCS_SIZE)


/********************************************************/
/* Beacon/Probersp capability field related definitions */
/********************************************************/

#define M80211_CAPABILITY_ESS               0x0001
#define M80211_CAPABILITY_IBSS              0x0002
#define M80211_CAPABILITY_CF_POLLABLE       0x0004
#define M80211_CAPABILITY_CF_POLL_REQUEST   0x0008
#define M80211_CAPABILITY_PRIVACY           0x0010
#define M80211_CAPABILITY_SHORT_PREAMBLE    0x0020
#define M80211_CAPABILITY_PBCC              0x0040
#define M80211_CAPABILITY_CHANNEL_AGILITY   0x0080
#define M80211_CAPABILITY_SPECTRUM_MGMT     0x0100
#define M80211_CAPABILITY_QOS               0x0200
#define M80211_CAPABILITY_SHORT_SLOTTIME    0x0400
#define M80211_CAPABILITY_APSD              0x0800
#define M80211_CAPABILITY_RADIO_MEASUREMENT 0x1000
#define M80211_CAPABILITY_DSS_OFDM          0x2000
#define M80211_CAPABILITY_DELAYED_BLOCK_ACK 0x4000
#define M80211_CAPABILITY_IMM_BLOCK_ACK     0x8000


/********************************/
/* Security related definitions */
/********************************/

typedef struct 
{
   uint8_t part[32];

} m80211_key_t;

typedef struct
{
   uint16_t lo;
   uint32_t hi;

} m80211_pn_t;

typedef struct
{
   uint8_t octet[6];

} pn_octet_t;

typedef union
{
   pn_octet_t octet;
   m80211_pn_t part;

} the_pn_t;

typedef uint8_t m80211_key_type_t;
#define M80211_KEY_TYPE_GROUP    0
#define M80211_KEY_TYPE_PAIRWISE 1
#define M80211_KEY_TYPE_STA      2
#define M80211_KEY_TYPE_ALL      3

typedef struct
{
   m80211_key_t          key;
   uint16_t              size;
   m80211_cipher_suite_t suite;
   the_pn_t              the_pn;
   uint48_lo32_t         replay_counter[4];
   bool_t                authenicator_initiator;

} m80211_cipher_key_t;

typedef struct
{
   m80211_key_t key;
}m80211_wep_key_t;

typedef struct 
{
  m80211_mac_addr_t dot11WepKeyMappingAddress;
  bool_t            dot11WepKeyMappingWepOn;
  m80211_wep_key_t  dot11WepKeyMappingValue;
  uint8_t           dot11WepKeyMappingRawStatus; 
}dot11WepKeyMappingEntry;


typedef struct
{
   m80211_wep_key_t dot11WepKeyMappingAddress;
}dot11WepDefaultKeyEntry;
 
typedef struct
{
   char prio;
   m80211_mac_addr_t addr2;
   pn_octet_t pn_octet; /* padded otherwise */
   /*m80211_pn_t pn;*/

} m80211_nonce_t;

typedef struct
{
   uint16_t frame_ctrl;
   m80211_mac_addr_t addr1;
   m80211_mac_addr_t addr2;
   m80211_mac_addr_t addr3;
   uint16_t          seq_ctrl;
/*    m80211_mac_addr_t addr4; */ /* Rethink this if we'll ever need addr4 -pek */
   uint16_t qc;
   uint8_t len;

} m80211_aad_t;

typedef struct
{
   m80211_nonce_t nonce;
   m80211_aad_t   aad;

} m80211_nonce_aad_t;

typedef struct
{
   uint16_t lo;
   uint8_t  rsvd;
   uint8_t  key_id;
   uint32_t hi;

} m80211_ccmp_hdr_t;

typedef struct
{
   char octet[3];
   char keyid;

} m80211_wep_iv_t;

typedef struct
{
   uint8_t tsc1;
   uint8_t wep_seed;
   uint8_t tsc0;
   uint8_t iv;
   uint8_t tsc2;
   uint8_t tsc3;
   uint8_t tsc4;
   uint8_t tsc5;

} m80211_tkip_iv_t;

typedef struct
{
   uint8_t octet[6];

} m80211_tkip_tsc_t;

typedef struct
{
   char octet[4];

} m80211_wep_icv_t;

typedef struct
{
   char octet[8];

} m80211_mic_t;

#define M80211_WPI_PN_SIZE  16
#define M80211_WPI_MIC_SIZE 16

typedef struct
{
   char octet[8];

} m80211_michael_t;


typedef struct
{
   uint32_t dot11TransmittedFragmentCount;
   uint32_t dot11MulticastTransmittedFrameCount;
   uint32_t dot11FailedCount;
   uint32_t dot11RetryCount;
   uint32_t dot11MultipleRetryCount;
   uint32_t dot11FrameDuplicateCount;
   uint32_t dot11RTSSuccessCount;
   uint32_t dot11RTSFailureCount;
   uint32_t dot11ACKFailureCount;
   uint32_t dot11ReceivedFragmentCount;
   uint32_t dot11MulticastReceivedFrameCount;
   uint32_t dot11FCSErrorCount;
   uint32_t dot11TransmittedFrameCount;
   uint32_t dot11WEPUndecryptableCount;
}dot11CountersEntry_t;


typedef struct
{
   uint32_t interferenceValueCounter;
}extendedCountersEntry_t;

/* Use the fact that the C-compiler will only execute the first statement if evaluated to "TRUE".
   Only when the lo part is increased to zero from its max value, the hi part will also be increased.
 */
#define M80211_PN_INC(pn) (void) (++((pn).lo) || ++((pn).hi))



/*****************/
/* Access Macros */
/*****************/
#define M80211_GET_IE_FLAG_HT_GREENFIELD(ht_capabilities_ie)\
   (((ht_capabilities_ie)->ht_capabilities_info[0] & M80211_HT_CAPABILITY_HT_GREENFIELD) ? TRUE : FALSE)


/* G L O B A L  V A R I A B L E S ********************************************/

/* I N T E R F A C E  F U N C T I O N S **************************************/
#endif    /* M80211_STDDEFS_H */
/* END OF FILE ***************************************************************/

