/*
 *   BSD LICENSE
 *
 *   Copyright(c) 2017 Cavium, Inc.. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Cavium, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER(S) OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*$FreeBSD$*/

/*
 * ! \file lio_response_manager.h
 *  \brief Host Driver:  Response queues for host instructions.
 */

#ifndef __LIO_RESPONSE_MANAGER_H__
#define __LIO_RESPONSE_MANAGER_H__

/*
 * Maximum ordered requests to process in every invocation of
 * lio_process_ordered_list(). The function will continue to process requests
 * as long as it can find one that has finished processing. If it keeps
 * finding requests that have completed, the function can run for ever. The
 * value defined here sets an upper limit on the number of requests it can
 * process before it returns control to the poll thread.
 */
#define LIO_MAX_ORD_REQS_TO_PROCESS   4096

/*
 * Head of a response list. There are several response lists in the
 * system. One for each response order- Unordered, ordered
 * and 1 for noresponse entries on each instruction queue.
 */

struct lio_response_list {
	/* List structure to add delete pending entries to */
	struct lio_stailq_head	head;

	/* A lock for this response list */
	struct mtx		lock;

	volatile int		pending_req_count;
};

/* The type of response list. */
enum {
	LIO_ORDERED_LIST		= 0,
	LIO_UNORDERED_NONBLOCKING_LIST	= 1,
	LIO_UNORDERED_BLOCKING_LIST	= 2,
	LIO_ORDERED_SC_LIST		= 3
};

/*
 * Error codes  used in Octeon Host-Core communication.
 *
 *   31            16 15            0
 *   ---------------------------------
 *   |               |               |
 *   ---------------------------------
 *   Error codes are 32-bit wide. The upper 16-bits, called Major Error Number,
 *   are reserved to identify the group to which the error code belongs. The
 *   lower 16-bits, called Minor Error Number, carry the actual code.
 *
 *   So error codes are (MAJOR NUMBER << 16)| MINOR_NUMBER.
 */

/*------   Error codes used by firmware (bits 15..0 set by firmware */
#define LIO_FW_MAJOR_ERROR_CODE         0x0001

/* A value of 0x00000000 indicates no error i.e. success */
#define LIO_DRIVER_ERROR_NONE                 0x00000000

#define LIO_DRIVER_ERROR_REQ_PENDING          0x00000001
#define LIO_DRIVER_ERROR_REQ_TIMEOUT          0x00000003
#define LIO_DRIVER_ERROR_REQ_EINTR            0x00000004

/*
 * Status for a request.
 * If a request is not queued to Octeon by the driver, the driver returns
 * an error condition that's describe by one of the OCTEON_REQ_ERR_* value
 * below. If the request is successfully queued, the driver will return
 * a LIO_REQUEST_PENDING status. LIO_REQUEST_TIMEOUT and
 * LIO_REQUEST_INTERRUPTED are only returned by the driver if the
 * response for request failed to arrive before a time-out period or if
 * the request processing * got interrupted due to a signal respectively.
 */
enum {
	LIO_REQUEST_DONE	= (LIO_DRIVER_ERROR_NONE),
	LIO_REQUEST_PENDING	= (LIO_DRIVER_ERROR_REQ_PENDING),
	LIO_REQUEST_TIMEOUT	= (LIO_DRIVER_ERROR_REQ_TIMEOUT),
	LIO_REQUEST_INTERRUPTED	= (LIO_DRIVER_ERROR_REQ_EINTR),
	LIO_REQUEST_NO_DEVICE	= (0x00000021),
	LIO_REQUEST_NOT_RUNNING,
	LIO_REQUEST_INVALID_IQ,
	LIO_REQUEST_INVALID_BUFCNT,
	LIO_REQUEST_INVALID_RESP_ORDER,
	LIO_REQUEST_NO_MEMORY,
	LIO_REQUEST_INVALID_BUFSIZE,
	LIO_REQUEST_NO_PENDING_ENTRY,
	LIO_REQUEST_NO_IQ_SPACE	= (0x7FFFFFFF)
};

#define LIO_STAILQ_FIRST_ENTRY(ptr, type, elem)	\
		(type *)((char *)((ptr)->stqh_first) - offsetof(type, elem))

#define LIO_FW_STATUS_CODE(status)		\
		((LIO_FW_MAJOR_ERROR_CODE << 16) | (status))

/*
 * Initialize the response lists. The number of response lists to create is
 * given by count.
 * @param octeon_dev      - the octeon device structure.
 */
int	lio_setup_response_list(struct octeon_device *octeon_dev);
void	lio_delete_response_list(struct octeon_device *octeon_dev);

/*
 * Check the status of first entry in the ordered list. If the instruction at
 * that entry finished processing or has timed-out, the entry is cleaned.
 * @param octeon_dev  - the octeon device structure.
 * @param force_quit - the request is forced to timeout if this is 1
 * @return 1 if the ordered list is empty, 0 otherwise.
 */
int	lio_process_ordered_list(struct octeon_device *octeon_dev,
				 uint32_t force_quit);

#endif	/* __LIO_RESPONSE_MANAGER_H__ */
