/*-
 * Copyright (c) 2016 Alexander Motin <mav@FreeBSD.org>
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
 * $FreeBSD$
 */

#ifndef _NTB_H_
#define _NTB_H_

#include "ntb_if.h"

extern devclass_t ntb_hw_devclass;
SYSCTL_DECL(_hw_ntb);

int ntb_register_device(device_t ntb);
int ntb_unregister_device(device_t ntb);
int ntb_child_location_str(device_t dev, device_t child, char *buf,
    size_t buflen);
int ntb_print_child(device_t dev, device_t child);

/*
 * ntb_link_event() - notify driver context of a change in link status
 * @ntb:        NTB device context
 *
 * Notify the driver context that the link status may have changed.  The driver
 * should call intb_link_is_up() to get the current status.
 */
void ntb_link_event(device_t ntb);

/*
 * ntb_db_event() - notify driver context of a doorbell event
 * @ntb:        NTB device context
 * @vector:     Interrupt vector number
 *
 * Notify the driver context of a doorbell event.  If hardware supports
 * multiple interrupt vectors for doorbells, the vector number indicates which
 * vector received the interrupt.  The vector number is relative to the first
 * vector used for doorbells, starting at zero, and must be less than
 * ntb_db_vector_count().  The driver may call ntb_db_read() to check which
 * doorbell bits need service, and ntb_db_vector_mask() to determine which of
 * those bits are associated with the vector number.
 */
void ntb_db_event(device_t ntb, uint32_t vec);

/*
 * ntb_link_is_up() - get the current ntb link state
 * @ntb:        NTB device context
 * @speed:      OUT - The link speed expressed as PCIe generation number
 * @width:      OUT - The link width expressed as the number of PCIe lanes
 *
 * RETURNS: true or false based on the hardware link state
 */
bool ntb_link_is_up(device_t ntb, enum ntb_speed *speed, enum ntb_width *width);

/*
 * ntb_link_enable() - enable the link on the secondary side of the ntb
 * @ntb:        NTB device context
 * @max_speed:  The maximum link speed expressed as PCIe generation number[0]
 * @max_width:  The maximum link width expressed as the number of PCIe lanes[0]
 *
 * Enable the link on the secondary side of the ntb.  This can only be done
 * from the primary side of the ntb in primary or b2b topology.  The ntb device
 * should train the link to its maximum speed and width, or the requested speed
 * and width, whichever is smaller, if supported.
 *
 * Return: Zero on success, otherwise an error number.
 *
 * [0]: Only NTB_SPEED_AUTO and NTB_WIDTH_AUTO are valid inputs; other speed
 *      and width input will be ignored.
 */
int ntb_link_enable(device_t ntb, enum ntb_speed speed, enum ntb_width width);

/*
 * ntb_link_disable() - disable the link on the secondary side of the ntb
 * @ntb:        NTB device context
 *
 * Disable the link on the secondary side of the ntb.  This can only be done
 * from the primary side of the ntb in primary or b2b topology.  The ntb device
 * should disable the link.  Returning from this call must indicate that a
 * barrier has passed, though with no more writes may pass in either direction
 * across the link, except if this call returns an error number.
 *
 * Return: Zero on success, otherwise an error number.
 */
int ntb_link_disable(device_t ntb);

/*
 * get enable status of the link on the secondary side of the ntb
 */
bool ntb_link_enabled(device_t ntb);

/*
 * ntb_set_ctx() - associate a driver context with an ntb device
 * @ntb:        NTB device context
 * @ctx:        Driver context
 * @ctx_ops:    Driver context operations
 *
 * Associate a driver context and operations with a ntb device.  The context is
 * provided by the client driver, and the driver may associate a different
 * context with each ntb device.
 *
 * Return: Zero if the context is associated, otherwise an error number.
 */
int ntb_set_ctx(device_t ntb, void *ctx, const struct ntb_ctx_ops *ctx_ops);

/*
 * ntb_set_ctx() - get a driver context associated with an ntb device
 * @ntb:        NTB device context
 * @ctx_ops:    Driver context operations
 *
 * Get a driver context and operations associated with a ntb device.
 */
void * ntb_get_ctx(device_t ntb, const struct ntb_ctx_ops **ctx_ops);

/*
 * ntb_clear_ctx() - disassociate any driver context from an ntb device
 * @ntb:        NTB device context
 *
 * Clear any association that may exist between a driver context and the ntb
 * device.
 */
void ntb_clear_ctx(device_t ntb);

/*
 * ntb_mw_count() - Get the number of memory windows available for KPI
 * consumers.
 *
 * (Excludes any MW wholly reserved for register access.)
 */
uint8_t ntb_mw_count(device_t ntb);

/*
 * ntb_mw_get_range() - get the range of a memory window
 * @ntb:        NTB device context
 * @idx:        Memory window number
 * @base:       OUT - the base address for mapping the memory window
 * @size:       OUT - the size for mapping the memory window
 * @align:      OUT - the base alignment for translating the memory window
 * @align_size: OUT - the size alignment for translating the memory window
 *
 * Get the range of a memory window.  NULL may be given for any output
 * parameter if the value is not needed.  The base and size may be used for
 * mapping the memory window, to access the peer memory.  The alignment and
 * size may be used for translating the memory window, for the peer to access
 * memory on the local system.
 *
 * Return: Zero on success, otherwise an error number.
 */
int ntb_mw_get_range(device_t ntb, unsigned mw_idx, vm_paddr_t *base,
    caddr_t *vbase, size_t *size, size_t *align, size_t *align_size,
    bus_addr_t *plimit);

/*
 * ntb_mw_set_trans() - set the translation of a memory window
 * @ntb:        NTB device context
 * @idx:        Memory window number
 * @addr:       The dma address local memory to expose to the peer
 * @size:       The size of the local memory to expose to the peer
 *
 * Set the translation of a memory window.  The peer may access local memory
 * through the window starting at the address, up to the size.  The address
 * must be aligned to the alignment specified by ntb_mw_get_range().  The size
 * must be aligned to the size alignment specified by ntb_mw_get_range().  The
 * address must be below the plimit specified by ntb_mw_get_range() (i.e. for
 * 32-bit BARs).
 *
 * Return: Zero on success, otherwise an error number.
 */
int ntb_mw_set_trans(device_t ntb, unsigned mw_idx, bus_addr_t addr,
    size_t size);

/*
 * ntb_mw_clear_trans() - clear the translation of a memory window
 * @ntb:	NTB device context
 * @idx:	Memory window number
 *
 * Clear the translation of a memory window.  The peer may no longer access
 * local memory through the window.
 *
 * Return: Zero on success, otherwise an error number.
 */
int ntb_mw_clear_trans(device_t ntb, unsigned mw_idx);

/*
 * ntb_mw_get_wc - Get the write-combine status of a memory window
 *
 * Returns:  Zero on success, setting *wc; otherwise an error number (e.g. if
 * idx is an invalid memory window).
 *
 * Mode is a VM_MEMATTR_* type.
 */
int ntb_mw_get_wc(device_t ntb, unsigned mw_idx, vm_memattr_t *mode);

/*
 * ntb_mw_set_wc - Set the write-combine status of a memory window
 *
 * If 'mode' matches the current status, this does nothing and succeeds.  Mode
 * is a VM_MEMATTR_* type.
 *
 * Returns:  Zero on success, setting the caching attribute on the virtual
 * mapping of the BAR; otherwise an error number (e.g. if idx is an invalid
 * memory window, or if changing the caching attribute fails).
 */
int ntb_mw_set_wc(device_t ntb, unsigned mw_idx, vm_memattr_t mode);

/*
 * ntb_spad_count() - get the total scratch regs usable
 * @ntb: pointer to ntb_softc instance
 *
 * This function returns the max 32bit scratchpad registers usable by the
 * upper layer.
 *
 * RETURNS: total number of scratch pad registers available
 */
uint8_t ntb_spad_count(device_t ntb);

/*
 * ntb_get_max_spads() - zero local scratch registers
 * @ntb: pointer to ntb_softc instance
 *
 * This functions overwrites all local scratchpad registers with zeroes.
 */
void ntb_spad_clear(device_t ntb);

/*
 * ntb_spad_write() - write to the secondary scratchpad register
 * @ntb: pointer to ntb_softc instance
 * @idx: index to the scratchpad register, 0 based
 * @val: the data value to put into the register
 *
 * This function allows writing of a 32bit value to the indexed scratchpad
 * register. The register resides on the secondary (external) side.
 *
 * RETURNS: An appropriate ERRNO error value on error, or zero for success.
 */
int ntb_spad_write(device_t ntb, unsigned int idx, uint32_t val);

/*
 * ntb_spad_read() - read from the primary scratchpad register
 * @ntb: pointer to ntb_softc instance
 * @idx: index to scratchpad register, 0 based
 * @val: pointer to 32bit integer for storing the register value
 *
 * This function allows reading of the 32bit scratchpad register on
 * the primary (internal) side.
 *
 * RETURNS: An appropriate ERRNO error value on error, or zero for success.
 */
int ntb_spad_read(device_t ntb, unsigned int idx, uint32_t *val);

/*
 * ntb_peer_spad_write() - write to the secondary scratchpad register
 * @ntb: pointer to ntb_softc instance
 * @idx: index to the scratchpad register, 0 based
 * @val: the data value to put into the register
 *
 * This function allows writing of a 32bit value to the indexed scratchpad
 * register. The register resides on the secondary (external) side.
 *
 * RETURNS: An appropriate ERRNO error value on error, or zero for success.
 */
int ntb_peer_spad_write(device_t ntb, unsigned int idx, uint32_t val);

/*
 * ntb_peer_spad_read() - read from the primary scratchpad register
 * @ntb: pointer to ntb_softc instance
 * @idx: index to scratchpad register, 0 based
 * @val: pointer to 32bit integer for storing the register value
 *
 * This function allows reading of the 32bit scratchpad register on
 * the primary (internal) side.
 *
 * RETURNS: An appropriate ERRNO error value on error, or zero for success.
 */
int ntb_peer_spad_read(device_t ntb, unsigned int idx, uint32_t *val);

/*
 * ntb_db_valid_mask() - get a mask of doorbell bits supported by the ntb
 * @ntb:	NTB device context
 *
 * Hardware may support different number or arrangement of doorbell bits.
 *
 * Return: A mask of doorbell bits supported by the ntb.
 */
uint64_t ntb_db_valid_mask(device_t ntb);

/*
 * ntb_db_vector_count() - get the number of doorbell interrupt vectors
 * @ntb:	NTB device context.
 *
 * Hardware may support different number of interrupt vectors.
 *
 * Return: The number of doorbell interrupt vectors.
 */
int ntb_db_vector_count(device_t ntb);

/*
 * ntb_db_vector_mask() - get a mask of doorbell bits serviced by a vector
 * @ntb:	NTB device context
 * @vector:	Doorbell vector number
 *
 * Each interrupt vector may have a different number or arrangement of bits.
 *
 * Return: A mask of doorbell bits serviced by a vector.
 */
uint64_t ntb_db_vector_mask(device_t ntb, uint32_t vector);

/*
 * ntb_peer_db_addr() - address and size of the peer doorbell register
 * @ntb:	NTB device context.
 * @db_addr:	OUT - The address of the peer doorbell register.
 * @db_size:	OUT - The number of bytes to write the peer doorbell register.
 *
 * Return the address of the peer doorbell register.  This may be used, for
 * example, by drivers that offload memory copy operations to a dma engine.
 * The drivers may wish to ring the peer doorbell at the completion of memory
 * copy operations.  For efficiency, and to simplify ordering of operations
 * between the dma memory copies and the ringing doorbell, the driver may
 * append one additional dma memory copy with the doorbell register as the
 * destination, after the memory copy operations.
 *
 * Return: Zero on success, otherwise an error number.
 *
 * Note that writing the peer doorbell via a memory window will *not* generate
 * an interrupt on the remote host; that must be done separately.
 */
int ntb_peer_db_addr(device_t ntb, bus_addr_t *db_addr, vm_size_t *db_size);

/*
 * ntb_db_clear() - clear bits in the local doorbell register
 * @ntb:	NTB device context.
 * @db_bits:	Doorbell bits to clear.
 *
 * Clear bits in the local doorbell register, arming the bits for the next
 * doorbell.
 *
 * Return: Zero on success, otherwise an error number.
 */
void ntb_db_clear(device_t ntb, uint64_t bits);

/*
 * ntb_db_clear_mask() - clear bits in the local doorbell mask
 * @ntb:	NTB device context.
 * @db_bits:	Doorbell bits to clear.
 *
 * Clear bits in the local doorbell mask register, allowing doorbell interrupts
 * from being generated for those doorbell bits.  If a doorbell bit is already
 * set at the time the mask is cleared, and the corresponding mask bit is
 * changed from set to clear, then the ntb driver must ensure that
 * ntb_db_event() is called.  If the hardware does not generate the interrupt
 * on clearing the mask bit, then the driver must call ntb_db_event() anyway.
 *
 * Return: Zero on success, otherwise an error number.
 */
void ntb_db_clear_mask(device_t ntb, uint64_t bits);

/*
 * ntb_db_read() - read the local doorbell register
 * @ntb:	NTB device context.
 *
 * Read the local doorbell register, and return the bits that are set.
 *
 * Return: The bits currently set in the local doorbell register.
 */
uint64_t ntb_db_read(device_t ntb);

/*
 * ntb_db_set_mask() - set bits in the local doorbell mask
 * @ntb:	NTB device context.
 * @db_bits:	Doorbell mask bits to set.
 *
 * Set bits in the local doorbell mask register, preventing doorbell interrupts
 * from being generated for those doorbell bits.  Bits that were already set
 * must remain set.
 *
 * Return: Zero on success, otherwise an error number.
 */
void ntb_db_set_mask(device_t ntb, uint64_t bits);

/*
 * ntb_peer_db_set() - Set the doorbell on the secondary/external side
 * @ntb: pointer to ntb_softc instance
 * @bit: doorbell bits to ring
 *
 * This function allows triggering of a doorbell on the secondary/external
 * side that will initiate an interrupt on the remote host
 */
void ntb_peer_db_set(device_t ntb, uint64_t bits);

#endif /* _NTB_H_ */
