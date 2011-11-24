/* $Id: nanodriver.h,v 1.1 2006-03-20 11:08:03 peek Exp $ */

#ifndef NANODRIVER_H
#define NANODRIVER_H
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
This header file exports the custom IOCTLs used for the NDIS driver

*****************************************************************************/

/*!
 * For the GET MIB IOCTL, the input buffer should start with the MIB
 * variable id string, followed by a NUL character. The return buffer
 * will contain 1 byte of status.
 */
#define IOCTL_NANO_MIB_GET_POST       \
        CTL_CODE(FILE_DEVICE_UNKNOWN, \
        0x800,                        \
        METHOD_BUFFERED,              \
        FILE_READ_DATA)

/*!
 * For the SET MIB IOCTL, the input buffer should start with the MIB
 * variable id string, followed by a NUL character, followed by the
 * input data. The return buffer will contain 1 byte of status.
 */
#define IOCTL_NANO_MIB_SET_POST       \
        CTL_CODE(FILE_DEVICE_UNKNOWN, \
        0x801,                        \
        METHOD_BUFFERED,              \
        FILE_READ_DATA)

#define IOCTL_NANO_MIB_RETRIEVE_RESPONSE   \
        CTL_CODE(FILE_DEVICE_UNKNOWN, \
        0x802,                        \
        METHOD_BUFFERED,              \
        FILE_READ_DATA)



#ifdef WITH_PACKET_HISTORY
/*!
 * The input buffer contains the offset in the read history
 * buffer from which to copy data. NANO_INVALID_LENGTH is
 * returned if the offset was out of bounds.
 * The output buffer contains the reuqested raw chunk of
 * the history buffer.
 */
#define IOCTL_NANO_GET_READ_HISTORY   \
        CTL_CODE(FILE_DEVICE_UNKNOWN, \
        0x803,                        \
        METHOD_BUFFERED,              \
        FILE_READ_DATA)

#define IOCTL_NANO_GET_WRITE_HISTORY   \
        CTL_CODE(FILE_DEVICE_UNKNOWN, \
        0x804,                        \
        METHOD_BUFFERED,              \
        FILE_READ_DATA)

#endif /* WITH_PACKET_HISTORY */

#define IOCTL_NANO_MIB_GET            \
        CTL_CODE(FILE_DEVICE_UNKNOWN, \
        0x805,                        \
        METHOD_BUFFERED,              \
        FILE_READ_DATA)

#define IOCTL_NANO_DRIVER_STATUS   \
        CTL_CODE(FILE_DEVICE_UNKNOWN, \
        0x806,                        \
        METHOD_BUFFERED,              \
        FILE_READ_DATA)

#define IOCTL_NANO_DRIVER_TICKLE   \
        CTL_CODE(FILE_DEVICE_UNKNOWN, \
        0x807,                        \
        METHOD_BUFFERED,              \
        FILE_READ_DATA)

#define IOCTL_NANO_LOAD_BOOTLOADER   \
        CTL_CODE(FILE_DEVICE_UNKNOWN, \
        0x808,                        \
        METHOD_BUFFERED,              \
        FILE_READ_DATA)

#define IOCTL_NANO_RESET_DEVICE   \
        CTL_CODE(FILE_DEVICE_UNKNOWN, \
        0x809,                        \
        METHOD_BUFFERED,              \
        FILE_READ_DATA)

#define IOCTL_NANO_POWER_SLEEP   \
        CTL_CODE(FILE_DEVICE_UNKNOWN, \
        0x810,                        \
        METHOD_BUFFERED,              \
        FILE_READ_DATA)

#define IOCTL_NANO_POWER_WAKE   \
        CTL_CODE(FILE_DEVICE_UNKNOWN, \
        0x811,                        \
        METHOD_BUFFERED,              \
        FILE_READ_DATA)

/* Response buffer is 1 byte status and 2 bytes sequence number (little-endian) */
#define IOCTL_NANO_SEND_CONSOLE_CMD   \
        CTL_CODE(FILE_DEVICE_UNKNOWN, \
        0x812,                        \
        METHOD_BUFFERED,              \
        FILE_READ_DATA)

#define IOCTL_NANO_GET_CONSOLE_REPLY   \
        CTL_CODE(FILE_DEVICE_UNKNOWN, \
        0x813,                        \
        METHOD_BUFFERED,              \
        FILE_READ_DATA)

/* IOCTL_NANO_* status values, they track internal values (from WiFiEngine) for
 * debugging purposes */
#define NANO_INVALID_DATA    -4 /* The MIB variable was invalid    */
#define NANO_NOT_ACCEPTED    -2 /* No reply could be found         */
#define NANO_INVALID_LENGTH  -1 /* The output buffer was too small */
#define NANO_FAILURE          0 /* Something else went wrong       */
#define NANO_SUCCESS          1

/* Status struct returned by IOCTL_NANO_DRIVER_STATUS */
struct driver_status {
      uint16_t tx_q_len;       /* Packets in the write queue */
      uint16_t tx_in_progress; /* Writes issued to hw but not completed */
      uint16_t state;          /* WiFiEngine state (enum driver_states in hmg.h) */
      uint16_t sub_state;      /* WiFiEngine sub-state */
      uint16_t cmdPendingFlag; /* Is the driver waiting for a command reply? */
      uint16_t dataPendingFlag;/* Is the driver waiting for one or more data confirm? */
      uint16_t dataPendingWinSize; /* Window size for outstanding data confirms */
      uint16_t rx_count;
      uint16_t rx_complete_ind_count;
      uint16_t tx_count;
      uint16_t tx_complete_ind_count;
      uint16_t state_log_len;
      char     state_log[1];
};


#endif /* NANODRIVER_H */
