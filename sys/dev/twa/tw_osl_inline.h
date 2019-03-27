/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-07 Applied Micro Circuits Corporation.
 * Copyright (c) 2004-05 Vinod Kashyap.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$FreeBSD$
 */

/*
 * AMCC'S 3ware driver for 9000 series storage controllers.
 *
 * Author: Vinod Kashyap
 * Modifications by: Adam Radford
 */



#ifndef TW_OSL_INLINE_H

#define TW_OSL_INLINE_H


/*
 * Inline functions shared between OSL and CL, and defined by OSL.
 */


#include <dev/twa/tw_osl.h>



/*
 * Function name:	tw_osl_init_lock
 * Description:		Initializes a lock.
 *
 * Input:		ctlr_handle	-- ptr to controller handle
 *			lock_name	-- string indicating name of the lock
 * Output:		lock		-- ptr to handle to the initialized lock
 * Return value:	None
 */
#define tw_osl_init_lock(ctlr_handle, lock_name, lock)	\
	mtx_init(lock, lock_name, NULL, MTX_SPIN)



/*
 * Function name:	tw_osl_destroy_lock
 * Description:		Destroys a previously initialized lock.
 *
 * Input:		ctlr_handle	-- ptr to controller handle
 *			lock		-- ptr to handle to the lock to be
 *						destroyed
 * Output:		None
 * Return value:	None
 */
#define tw_osl_destroy_lock(ctlr_handle, lock)	\
	mtx_destroy(lock)



/*
 * Function name:	tw_osl_get_lock
 * Description:		Acquires the specified lock.
 *
 * Input:		ctlr_handle	-- ptr to controller handle
 *			lock		-- ptr to handle to the lock to be
 *						acquired
 * Output:		None
 * Return value:	None
 */
#define tw_osl_get_lock(ctlr_handle, lock)	\
	mtx_lock_spin(lock)



/*
 * Function name:	tw_osl_free_lock
 * Description:		Frees a previously acquired lock.
 *
 * Input:		ctlr_handle	-- ptr to controller handle
 *			lock		-- ptr to handle to the lock to be freed
 * Output:		None
 * Return value:	None
 */
#define tw_osl_free_lock(ctlr_handle, lock)	\
	mtx_unlock_spin(lock)



#ifdef TW_OSL_DEBUG

/*
 * Function name:	tw_osl_dbg_printf
 * Description:		Prints passed info (prefixed by ctlr name)to syslog
 *
 * Input:		ctlr_handle -- controller handle
 *			fmt -- format string for the arguments to follow
 *			... -- variable number of arguments, to be printed
 *				based on the fmt string
 * Output:		None
 * Return value:	Number of bytes printed
 */
#define tw_osl_dbg_printf(ctlr_handle, fmt, args...)			\
	twa_printf((ctlr_handle->osl_ctlr_ctxt), fmt, ##args)

#endif /* TW_OSL_DEBUG */



/*
 * Function name:	tw_osl_notify_event
 * Description:		Prints passed event info (prefixed by ctlr name)
 *			to syslog
 *
 * Input:		ctlr_handle -- controller handle
 *			event -- ptr to a packet describing the event/error
 * Output:		None
 * Return value:	None
 */
#define tw_osl_notify_event(ctlr_handle, event)				\
	twa_printf((ctlr_handle->osl_ctlr_ctxt),			\
		"%s: (0x%02X: 0x%04X): %s: %s\n",			\
		event->severity_str,					\
		event->event_src,					\
		event->aen_code,					\
		event->parameter_data +					\
			strlen(event->parameter_data) + 1,		\
		event->parameter_data)



/*
 * Function name:	tw_osl_read_reg
 * Description:		Reads a register on the controller
 *
 * Input:		ctlr_handle -- controller handle
 *			offset -- offset from Base Address
 *			size -- # of bytes to read
 * Output:		None
 * Return value:	Value read
 */
#define tw_osl_read_reg		tw_osl_read_reg_inline
static __inline TW_UINT32
tw_osl_read_reg_inline(struct tw_cl_ctlr_handle *ctlr_handle,
	TW_INT32 offset, TW_INT32 size)
{
	bus_space_tag_t		bus_tag =
		((struct twa_softc *)(ctlr_handle->osl_ctlr_ctxt))->bus_tag;
	bus_space_handle_t	bus_handle =
		((struct twa_softc *)(ctlr_handle->osl_ctlr_ctxt))->bus_handle;

	if (size == 4)
		return((TW_UINT32)bus_space_read_4(bus_tag, bus_handle,
			offset));
	else if (size == 2)
		return((TW_UINT32)bus_space_read_2(bus_tag, bus_handle,
			offset));
	else
		return((TW_UINT32)bus_space_read_1(bus_tag, bus_handle,
			offset));
}



/*
 * Function name:	tw_osl_write_reg
 * Description:		Writes to a register on the controller
 *
 * Input:		ctlr_handle -- controller handle
 *			offset -- offset from Base Address
 *			value -- value to write
 *			size -- # of bytes to write
 * Output:		None
 * Return value:	None
 */
#define tw_osl_write_reg	tw_osl_write_reg_inline
static __inline TW_VOID
tw_osl_write_reg_inline(struct tw_cl_ctlr_handle *ctlr_handle,
	TW_INT32 offset, TW_INT32 value, TW_INT32 size)
{
	bus_space_tag_t		bus_tag =
		((struct twa_softc *)(ctlr_handle->osl_ctlr_ctxt))->bus_tag;
	bus_space_handle_t	bus_handle =
		((struct twa_softc *)(ctlr_handle->osl_ctlr_ctxt))->bus_handle;

	if (size == 4)
		bus_space_write_4(bus_tag, bus_handle, offset, value);
	else if (size == 2)
		bus_space_write_2(bus_tag, bus_handle, offset, (TW_INT16)value);
	else
		bus_space_write_1(bus_tag, bus_handle, offset, (TW_INT8)value);
}



#ifdef TW_OSL_PCI_CONFIG_ACCESSIBLE

/*
 * Function name:	tw_osl_read_pci_config
 * Description:		Reads from the PCI config space.
 *
 * Input:		sc	-- ptr to per ctlr structure
 *			offset	-- register offset
 *			size	-- # of bytes to be read
 * Output:		None
 * Return value:	Value read
 */
#define tw_osl_read_pci_config(ctlr_handle, offset, size)		\
	pci_read_config(						\
		((struct twa_softc *)(ctlr_handle->osl_ctlr_ctxt))->bus_dev, \
		offset, size)



/*
 * Function name:	tw_osl_write_pci_config
 * Description:		Writes to the PCI config space.
 *
 * Input:		sc	-- ptr to per ctlr structure
 *			offset	-- register offset
 *			value	-- value to write
 *			size	-- # of bytes to be written
 * Output:		None
 * Return value:	None
 */
#define tw_osl_write_pci_config(ctlr_handle, offset, value, size)	\
	pci_write_config(						\
		((struct twa_softc *)(ctlr_handle->osl_ctlr_ctxt))->bus_dev, \
		offset/*PCIR_STATUS*/, value, size)

#endif /* TW_OSL_PCI_CONFIG_ACCESSIBLE */



/*
 * Function name:	tw_osl_get_local_time
 * Description:		Gets the local time
 *
 * Input:		None
 * Output:		None
 * Return value:	local time
 */
#define tw_osl_get_local_time()						\
	(time_second - utc_offset())


/*
 * Function name:	tw_osl_delay
 * Description:		Spin for the specified time
 *
 * Input:		usecs -- micro-seconds to spin
 * Output:		None
 * Return value:	None
 */
#define tw_osl_delay(usecs)	DELAY(usecs)



#ifdef TW_OSL_CAN_SLEEP

/*
 * Function name:	tw_osl_sleep
 * Description:		Sleep for the specified time, or until woken up
 *
 * Input:		ctlr_handle -- controller handle
 *			sleep_handle -- handle to sleep on
 *			timeout -- time period (in ms) to sleep
 * Output:		None
 * Return value:	0 -- successfully woken up
 *			EWOULDBLOCK -- time out
 *			ERESTART -- woken up by a signal
 */
#define tw_osl_sleep(ctlr_handle, sleep_handle, timeout)		\
	tsleep((TW_VOID *)sleep_handle, PRIBIO, NULL, timeout)



/*
 * Function name:	tw_osl_wakeup
 * Description:		Wake up a sleeping process
 *
 * Input:		ctlr_handle -- controller handle
 *			sleep_handle -- handle of sleeping process to be
					woken up
 * Output:		None
 * Return value:	None
 */
#define tw_osl_wakeup(ctlr_handle, sleep_handle)			\
	wakeup_one(sleep_handle)

#endif /* TW_OSL_CAN_SLEEP */



/* Allows setting breakpoints in the CL code for debugging purposes. */
#define tw_osl_breakpoint()		breakpoint()


/* Text name of current function. */
#define tw_osl_cur_func()		__func__


/* Copy 'size' bytes from 'src' to 'dest'. */
#define tw_osl_memcpy(dest, src, size)	bcopy(src, dest, size)


/* Zero 'size' bytes starting at 'addr'. */
#define tw_osl_memzero			bzero


/* Standard sprintf. */
#define tw_osl_sprintf			sprintf


/* Copy string 'src' to 'dest'. */
#define tw_osl_strcpy			strcpy


/* Return length of string pointed at by 'str'. */
#define tw_osl_strlen			strlen


/* Standard vsprintf. */
#define tw_osl_vsprintf			vsprintf



#endif /* TW_OSL_INLINE_H */
