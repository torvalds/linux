/* SPDX-License-Identifier: GPL-2.0-only */
/******************************************************************************

    AudioScience HPI driver
    Copyright (C) 1997-2011  AudioScience Inc. <support@audioscience.com>


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
