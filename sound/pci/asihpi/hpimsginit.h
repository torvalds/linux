/******************************************************************************

    AudioScience HPI driver
    Copyright (C) 1997-2011  AudioScience Inc. <support@audioscience.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of version 2 of the GNU General Public License as
    published by the Free Software Foundation;

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

 Hardware Programming Interface (HPI) Utility functions

 (C) Copyright AudioScience Inc. 2007
*******************************************************************************/
/* Initialise response headers, or msg/response pairs.
Note that it is valid to just init a response e.g. when a lower level is
preparing a response to a message.
However, when sending a message, a matching response buffer must always be
prepared.
*/

#ifndef _HPIMSGINIT_H_
#define _HPIMSGINIT_H_

void hpi_init_response(struct hpi_response *phr, u16 object, u16 function,
	u16 error);

void hpi_init_message_response(struct hpi_message *phm,
	struct hpi_response *phr, u16 object, u16 function);

void hpi_init_responseV1(struct hpi_response_header *phr, u16 size,
	u16 object, u16 function);

void hpi_init_message_responseV1(struct hpi_message_header *phm, u16 msg_size,
	struct hpi_response_header *phr, u16 res_size, u16 object,
	u16 function);

#endif				/* _HPIMSGINIT_H_ */
