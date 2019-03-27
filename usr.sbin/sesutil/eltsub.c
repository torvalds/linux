/* $FreeBSD$ */
/*
 * Copyright (c) 2000 by Matthew Jacob
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * the GNU Public License ("GPL").
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * Matthew Jacob
 * Feral Software
 * mjacob@feral.com
 */

#include <sys/types.h>

#include <err.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <cam/scsi/scsi_enc.h>
#include <libxo/xo.h>

#include "eltsub.h"

/*
 * offset by +20 degrees.
 * The range of the value expresses a temperature between -19 and +235 degrees
 * Celsius. A value of 00h is reserved.
 */
#define TEMPERATURE_OFFSET 20

const char *
geteltnm(int type)
{
	static char rbuf[132];

	switch (type) {
	case ELMTYP_UNSPECIFIED:
		return ("Unspecified");
	case ELMTYP_DEVICE:
		return ("Device Slot");
	case ELMTYP_POWER:
		return ("Power Supply");
	case ELMTYP_FAN:
		return ("Cooling");
	case ELMTYP_THERM:
		return ("Temperature Sensors");
	case ELMTYP_DOORLOCK:
		return ("Door Lock");
	case ELMTYP_ALARM:
		return ("Audible alarm");
	case ELMTYP_ESCC:
		return ("Enclosure Services Controller Electronics");
	case ELMTYP_SCC:
		return ("SCC Controller Electronics");
	case ELMTYP_NVRAM:
		return ("Nonvolatile Cache");
	case ELMTYP_INV_OP_REASON:
		return ("Invalid Operation Reason");
	case ELMTYP_UPS:
		return ("Uninterruptible Power Supply");
	case ELMTYP_DISPLAY:
		return ("Display");
	case ELMTYP_KEYPAD:
		return ("Key Pad Entry");
	case ELMTYP_ENCLOSURE:
		return ("Enclosure");
	case ELMTYP_SCSIXVR:
		return ("SCSI Port/Transceiver");
	case ELMTYP_LANGUAGE:
		return ("Language");
	case ELMTYP_COMPORT:
		return ("Communication Port");
	case ELMTYP_VOM:
		return ("Voltage Sensor");
	case ELMTYP_AMMETER:
		return ("Current Sensor");
	case ELMTYP_SCSI_TGT:
		return ("SCSI Target Port");
	case ELMTYP_SCSI_INI:
		return ("SCSI Initiator Port");
	case ELMTYP_SUBENC:
		return ("Simple Subenclosure");
	case ELMTYP_ARRAY_DEV:
		return ("Array Device Slot");
	case ELMTYP_SAS_EXP:
		return ("SAS Expander");
	case ELMTYP_SAS_CONN:
		return ("SAS Connector");
	default:
		snprintf(rbuf, sizeof(rbuf), "<Type 0x%x>", type);
		return (rbuf);
	}
}

const char *
scode2ascii(u_char code)
{
	static char rbuf[32];
	switch (code & 0xf) {
	case SES_OBJSTAT_UNSUPPORTED:
		return ("Unsupported");
	case SES_OBJSTAT_OK:
		return ("OK");
	case SES_OBJSTAT_CRIT:
		return ("Critical");
	case SES_OBJSTAT_NONCRIT:
		return ("Noncritical");
	case SES_OBJSTAT_UNRECOV:
		return ("Unrecoverable");
	case SES_OBJSTAT_NOTINSTALLED:
		return ("Not Installed");
	case SES_OBJSTAT_UNKNOWN:
		return ("Unknown");
	case SES_OBJSTAT_NOTAVAIL:
		return ("Not Available");
	case SES_OBJSTAT_NOACCESS:
		return ("No Access Allowed");
	default:
		snprintf(rbuf, sizeof(rbuf), "<Status 0x%x>", code & 0xf);
		return (rbuf);
	}
}
