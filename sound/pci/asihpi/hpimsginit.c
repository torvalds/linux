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

 Hardware Programming Interface (HPI) Utility functions.

 (C) Copyright AudioScience Inc. 2007
*******************************************************************************/

#include "hpi_internal.h"
#include "hpimsginit.h"

/* The actual message size for each object type */
static u16 msg_size[HPI_OBJ_MAXINDEX + 1] = HPI_MESSAGE_SIZE_BY_OBJECT;
/* The actual response size for each object type */
static u16 res_size[HPI_OBJ_MAXINDEX + 1] = HPI_RESPONSE_SIZE_BY_OBJECT;
/* Flag to enable alternate message type for SSX2 bypass. */
static u16 gwSSX2_bypass;

/** \internal
  * initialize the HPI message structure
  */
static void hpi_init_message(struct hpi_message *phm, u16 object,
	u16 function)
{
	memset(phm, 0, sizeof(*phm));
	if ((object > 0) && (object <= HPI_OBJ_MAXINDEX))
		phm->size = msg_size[object];
	else
		phm->size = sizeof(*phm);

	if (gwSSX2_bypass)
		phm->type = HPI_TYPE_SSX2BYPASS_MESSAGE;
	else
		phm->type = HPI_TYPE_REQUEST;
	phm->object = object;
	phm->function = function;
	phm->version = 0;
	phm->adapter_index = HPI_ADAPTER_INDEX_INVALID;
	/* Expect actual adapter index to be set by caller */
}

/** \internal
  * initialize the HPI response structure
  */
void hpi_init_response(struct hpi_response *phr, u16 object, u16 function,
	u16 error)
{
	memset(phr, 0, sizeof(*phr));
	phr->type = HPI_TYPE_RESPONSE;
	if ((object > 0) && (object <= HPI_OBJ_MAXINDEX))
		phr->size = res_size[object];
	else
		phr->size = sizeof(*phr);
	phr->object = object;
	phr->function = function;
	phr->error = error;
	phr->specific_error = 0;
	phr->version = 0;
}

void hpi_init_message_response(struct hpi_message *phm,
	struct hpi_response *phr, u16 object, u16 function)
{
	hpi_init_message(phm, object, function);
	/* default error return if the response is
	   not filled in by the callee */
	hpi_init_response(phr, object, function,
		HPI_ERROR_PROCESSING_MESSAGE);
}

static void hpi_init_messageV1(struct hpi_message_header *phm, u16 size,
	u16 object, u16 function)
{
	memset(phm, 0, sizeof(*phm));
	if ((object > 0) && (object <= HPI_OBJ_MAXINDEX)) {
		phm->size = size;
		phm->type = HPI_TYPE_REQUEST;
		phm->object = object;
		phm->function = function;
		phm->version = 1;
		/* Expect adapter index to be set by caller */
	}
}

void hpi_init_responseV1(struct hpi_response_header *phr, u16 size,
	u16 object, u16 function)
{
	memset(phr, 0, sizeof(*phr));
	phr->size = size;
	phr->version = 1;
	phr->type = HPI_TYPE_RESPONSE;
	phr->error = HPI_ERROR_PROCESSING_MESSAGE;
}

void hpi_init_message_responseV1(struct hpi_message_header *phm, u16 msg_size,
	struct hpi_response_header *phr, u16 res_size, u16 object,
	u16 function)
{
	hpi_init_messageV1(phm, msg_size, object, function);
	hpi_init_responseV1(phr, res_size, object, function);
}
