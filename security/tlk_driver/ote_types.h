/*
 * Copyright (c) 2013 NVIDIA Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __OTE_TYPES_H__
#define __OTE_TYPES_H__

/*
 * Return Codes
 */
enum {
	/* Success */
	OTE_SUCCESS			= 0x00000000,
	OTE_ERROR_NO_ERROR		= OTE_SUCCESS,
	/* Non-specific cause */
	OTE_ERROR_GENERIC		= 0xFFFF0000,
	/* Access priviledge not sufficient */
	OTE_ERROR_ACCESS_DENIED		= 0xFFFF0001,
	/* The operation was cancelled */
	OTE_ERROR_CANCEL		= 0xFFFF0002,
	/* Concurrent accesses conflict */
	OTE_ERROR_ACCESS_CONFLICT	= 0xFFFF0003,
	/* Too much data for req was passed */
	OTE_ERROR_EXCESS_DATA		= 0xFFFF0004,
	/* Input data was of invalid format */
	OTE_ERROR_BAD_FORMAT		= 0xFFFF0005,
	/* Input parameters were invalid */
	OTE_ERROR_BAD_PARAMETERS	= 0xFFFF0006,
	/* Oper invalid in current state */
	OTE_ERROR_BAD_STATE		= 0xFFFF0007,
	/* The req data item not found */
	OTE_ERROR_ITEM_NOT_FOUND	= 0xFFFF0008,
	/* The req oper not implemented */
	OTE_ERROR_NOT_IMPLEMENTED	= 0xFFFF0009,
	/* The req oper not supported */
	OTE_ERROR_NOT_SUPPORTED		= 0xFFFF000A,
	/* Expected data was missing */
	OTE_ERROR_NO_DATA		= 0xFFFF000B,
	/* System ran out of resources */
	OTE_ERROR_OUT_OF_MEMORY		= 0xFFFF000C,
	/* The system is busy */
	OTE_ERROR_BUSY			= 0xFFFF000D,
	/* Communication failed */
	OTE_ERROR_COMMUNICATION		= 0xFFFF000E,
	/* A security fault was detected */
	OTE_ERROR_SECURITY		= 0xFFFF000F,
	/* The supplied buffer is too short */
	OTE_ERROR_SHORT_BUFFER		= 0xFFFF0010,
};

/*
 * Return Code origins
 */
enum {
	/* Originated from OTE Client API */
	OTE_RESULT_ORIGIN_API = 1,
	/* Originated from Underlying Communication Stack */
	OTE_RESULT_ORIGIN_COMMS = 2,
	/* Originated from Common OTE Code */
	OTE_RESULT_ORIGIN_KERNEL = 3,
	/* Originated from Trusted APP Code */
	OTE_RESULT_ORIGIN_TRUSTED_APP = 4,
};

#endif
