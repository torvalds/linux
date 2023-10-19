// SPDX-License-Identifier: GPL-2.0-only
/******************************************************************************

    AudioScience HPI driver
    Copyright (C) 1997-2014  AudioScience Inc. <support@audioscience.com>


 Hardware Programming Interface (HPI) Utility functions.

 (C) Copyright AudioScience Inc. 2007
*******************************************************************************/

#include "hpi_internal.h"
#include "hpimsginit.h"
#include <linux/nospec.h>

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
	u16 size;

	if ((object > 0) && (object <= HPI_OBJ_MAXINDEX)) {
		object = array_index_nospec(object, HPI_OBJ_MAXINDEX + 1);
		size = msg_size[object];
	} else {
		size = sizeof(*phm);
	}

	memset(phm, 0, size);
	phm->size = size;

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
	u16 size;

	if ((object > 0) && (object <= HPI_OBJ_MAXINDEX)) {
		object = array_index_nospec(object, HPI_OBJ_MAXINDEX + 1);
		size = res_size[object];
	} else {
		size = sizeof(*phr);
	}

	memset(phr, 0, sizeof(*phr));
	phr->size = size;
	phr->type = HPI_TYPE_RESPONSE;
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
	memset(phm, 0, size);
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
	(void)object;
	(void)function;
	memset(phr, 0, size);
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
