/*
 * ca.h
 *
 * Copyright (C) 2000 Ralph  Metzler <ralph@convergence.de>
 *                  & Marcus Metzler <marcus@convergence.de>
 *                    for convergence integrated media GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Lesser Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#ifndef _DVBCA_H_
#define _DVBCA_H_

/* slot interface types and info */

typedef struct ca_slot_info {
        int num;               /* slot number */

        int type;              /* CA interface this slot supports */
#define CA_CI            1     /* CI high level interface */
#define CA_CI_LINK       2     /* CI link layer level interface */
#define CA_CI_PHYS       4     /* CI physical layer level interface */
#define CA_DESCR         8     /* built-in descrambler */
#define CA_SC          128     /* simple smart card interface */

        unsigned int flags;
#define CA_CI_MODULE_PRESENT 1 /* module (or card) inserted */
#define CA_CI_MODULE_READY   2
} ca_slot_info_t;


/* descrambler types and info */

typedef struct ca_descr_info {
        unsigned int num;          /* number of available descramblers (keys) */
        unsigned int type;         /* type of supported scrambling system */
#define CA_ECD           1
#define CA_NDS           2
#define CA_DSS           4
} ca_descr_info_t;

typedef struct ca_caps {
        unsigned int slot_num;     /* total number of CA card and module slots */
        unsigned int slot_type;    /* OR of all supported types */
        unsigned int descr_num;    /* total number of descrambler slots (keys) */
        unsigned int descr_type;   /* OR of all supported types */
} ca_caps_t;

/* a message to/from a CI-CAM */
typedef struct ca_msg {
        unsigned int index;
        unsigned int type;
        unsigned int length;
        unsigned char msg[256];
} ca_msg_t;

typedef struct ca_descr {
        unsigned int index;
        unsigned int parity;	/* 0 == even, 1 == odd */
        unsigned char cw[8];
} ca_descr_t;

typedef struct ca_pid {
        unsigned int pid;
        int index;		/* -1 == disable*/
} ca_pid_t;

#define CA_RESET          _IO('o', 128)
#define CA_GET_CAP        _IOR('o', 129, ca_caps_t)
#define CA_GET_SLOT_INFO  _IOR('o', 130, ca_slot_info_t)
#define CA_GET_DESCR_INFO _IOR('o', 131, ca_descr_info_t)
#define CA_GET_MSG        _IOR('o', 132, ca_msg_t)
#define CA_SEND_MSG       _IOW('o', 133, ca_msg_t)
#define CA_SET_DESCR      _IOW('o', 134, ca_descr_t)
#define CA_SET_PID        _IOW('o', 135, ca_pid_t)

#endif
