/*
 *  The NFC Controller Interface is the communication protocol between an
 *  NFC Controller (NFCC) and a Device Host (DH).
 *
 *  Copyright (C) 2011 Texas Instruments, Inc.
 *
 *  Written by Ilan Elias <ilane@ti.com>
 *
 *  Acknowledgements:
 *  This file is based on lib.c, which was written
 *  by Maxim Krasnyansky.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2
 *  as published by the Free Software Foundation
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>

#include <net/nfc/nci.h>
#include <net/nfc/nci_core.h>

/* NCI status codes to Unix errno mapping */
int nci_to_errno(__u8 code)
{
	switch (code) {
	case NCI_STATUS_OK:
		return 0;

	case NCI_STATUS_REJECTED:
		return -EBUSY;

	case NCI_STATUS_RF_FRAME_CORRUPTED:
		return -EBADMSG;

	case NCI_STATUS_NOT_INITIALIZED:
		return -EHOSTDOWN;

	case NCI_STATUS_SYNTAX_ERROR:
	case NCI_STATUS_SEMANTIC_ERROR:
	case NCI_STATUS_INVALID_PARAM:
	case NCI_STATUS_RF_PROTOCOL_ERROR:
	case NCI_STATUS_NFCEE_PROTOCOL_ERROR:
		return -EPROTO;

	case NCI_STATUS_UNKNOWN_GID:
	case NCI_STATUS_UNKNOWN_OID:
		return -EBADRQC;

	case NCI_STATUS_MESSAGE_SIZE_EXCEEDED:
		return -EMSGSIZE;

	case NCI_STATUS_DISCOVERY_ALREADY_STARTED:
		return -EALREADY;

	case NCI_STATUS_DISCOVERY_TARGET_ACTIVATION_FAILED:
	case NCI_STATUS_NFCEE_INTERFACE_ACTIVATION_FAILED:
		return -ECONNREFUSED;

	case NCI_STATUS_RF_TRANSMISSION_ERROR:
	case NCI_STATUS_NFCEE_TRANSMISSION_ERROR:
		return -ECOMM;

	case NCI_STATUS_RF_TIMEOUT_ERROR:
	case NCI_STATUS_NFCEE_TIMEOUT_ERROR:
		return -ETIMEDOUT;

	case NCI_STATUS_FAILED:
	default:
		return -ENOSYS;
	}
}
EXPORT_SYMBOL(nci_to_errno);
