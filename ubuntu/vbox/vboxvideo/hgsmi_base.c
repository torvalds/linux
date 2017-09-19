/*
 * Copyright (C) 2006-2017 Oracle Corporation
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "vbox_drv.h"
#include "vbox_err.h"
#include "vboxvideo_guest.h"
#include "vboxvideo_vbe.h"
#include "hgsmi_channels.h"
#include "hgsmi_ch_setup.h"

/** Detect whether HGSMI is supported by the host. */
bool VBoxHGSMIIsSupported(void)
{
	u16 DispiId;

	outw(VBE_DISPI_INDEX_ID, VBE_DISPI_IOPORT_INDEX);
	outw(VBE_DISPI_ID_HGSMI, VBE_DISPI_IOPORT_DATA);

	DispiId = inw(VBE_DISPI_IOPORT_DATA);

	return (DispiId == VBE_DISPI_ID_HGSMI);
}


/**
 * Inform the host of the location of the host flags in VRAM via an HGSMI command.
 * @returns  IPRT status value.
 * @returns  VERR_NOT_IMPLEMENTED  if the host does not support the command.
 * @returns  VERR_NO_MEMORY        if a heap allocation fails.
 * @param    ctx                  the context of the guest heap to use.
 * @param    location           the offset chosen for the flags withing guest VRAM.
 */
int hgsmi_report_flags_location(struct gen_pool * ctx, u32 location)
{

	/* Allocate the IO buffer. */
	struct hgsmi_buffer_location  *p =
		(struct hgsmi_buffer_location  *)hgsmi_buffer_alloc(ctx, sizeof(*p), HGSMI_CH_HGSMI,
						HGSMI_CC_HOST_FLAGS_LOCATION);
	if (!p)
		return VERR_NO_MEMORY;

	/* Prepare data to be sent to the host. */
	p->location = location;
	p->buf_len  = sizeof(struct hgsmi_host_flags);
	/* No need to check that the buffer is valid as we have just allocated it. */
	hgsmi_buffer_submit(ctx, p);
	/* Free the IO buffer. */
	hgsmi_buffer_free(ctx, p);

	return VINF_SUCCESS;
}


/**
 * Notify the host of HGSMI-related guest capabilities via an HGSMI command.
 * @returns  IPRT status value.
 * @returns  VERR_NOT_IMPLEMENTED  if the host does not support the command.
 * @returns  VERR_NO_MEMORY        if a heap allocation fails.
 * @param    ctx                  the context of the guest heap to use.
 * @param    caps                 the capabilities to report, see struct vbva_caps.
 */
int hgsmi_send_caps_info(struct gen_pool * ctx, u32 caps)
{

	/* Allocate the IO buffer. */
	struct vbva_caps  *p =
		(struct vbva_caps  *)hgsmi_buffer_alloc(ctx, sizeof(*p), HGSMI_CH_VBVA, VBVA_INFO_CAPS);

	if (!p)
		return VERR_NO_MEMORY;

	/* Prepare data to be sent to the host. */
	p->rc    = VERR_NOT_IMPLEMENTED;
	p->caps = caps;
	/* No need to check that the buffer is valid as we have just allocated it. */
	hgsmi_buffer_submit(ctx, p);

	WARN_ON_ONCE(RT_FAILURE(p->rc));
	/* Free the IO buffer. */
	hgsmi_buffer_free(ctx, p);
	return p->rc;
}


/**
 * Get the information needed to map the basic communication structures in
 * device memory into our address space.  All pointer parameters are optional.
 *
 * @param  cbVRAM               how much video RAM is allocated to the device
 * @param  poffVRAMBaseMapping  where to save the offset from the start of the
 *                              device VRAM of the whole area to map
 * @param  pcbMapping           where to save the mapping size
 * @param  poffGuestHeapMemory  where to save the offset into the mapped area
 *                              of the guest heap backing memory
 * @param  pcbGuestHeapMemory   where to save the size of the guest heap
 *                              backing memory
 * @param  poffHostFlags        where to save the offset into the mapped area
 *                              of the host flags
 */
void VBoxHGSMIGetBaseMappingInfo(u32 cbVRAM,
						u32 *poffVRAMBaseMapping,
						u32 *pcbMapping,
						u32 *poffGuestHeapMemory,
						u32 *pcbGuestHeapMemory,
						u32 *poffHostFlags)
{
	if (poffVRAMBaseMapping)
		*poffVRAMBaseMapping = cbVRAM - VBVA_ADAPTER_INFORMATION_SIZE;
	if (pcbMapping)
		*pcbMapping = VBVA_ADAPTER_INFORMATION_SIZE;
	if (poffGuestHeapMemory)
		*poffGuestHeapMemory = 0;
	if (pcbGuestHeapMemory)
		*pcbGuestHeapMemory =   VBVA_ADAPTER_INFORMATION_SIZE
						- sizeof(struct hgsmi_host_flags);
	if (poffHostFlags)
		*poffHostFlags =   VBVA_ADAPTER_INFORMATION_SIZE
						- sizeof(struct hgsmi_host_flags);
}

/**
 * Query the host for an HGSMI configuration parameter via an HGSMI command.
 * @returns iprt status value
 * @param  ctx      the context containing the heap used
 * @param  index  the index of the parameter to query,
 *                   @see struct vbva_conf32::index
 * @param  value_ret  where to store the value of the parameter on success
 */
int hgsmi_query_conf(struct gen_pool * ctx, u32 index, u32 *value_ret)
{
	struct vbva_conf32 *p;

	/* Allocate the IO buffer. */
	p = hgsmi_buffer_alloc(ctx, sizeof(*p), HGSMI_CH_VBVA, VBVA_QUERY_CONF32);
	if (!p)
		return VERR_NO_MEMORY;

	/* Prepare data to be sent to the host. */
	p->index = index;
	p->value = U32_MAX;
	/* No need to check that the buffer is valid as we have just allocated it. */
	hgsmi_buffer_submit(ctx, p);
	*value_ret = p->value;
	/* Free the IO buffer. */
	hgsmi_buffer_free(ctx, p);
	return VINF_SUCCESS;
}

/**
 * Pass the host a new mouse pointer shape via an HGSMI command.
 *
 * @returns  success or failure
 * @param  ctx      the context containing the heap to be used
 * @param  flags    cursor flags, @see VMMDevReqMousePointer::flags
 * @param  hot_x     horizontal position of the hot spot
 * @param  hot_y     vertical position of the hot spot
 * @param  width    width in pixels of the cursor
 * @param  height   height in pixels of the cursor
 * @param  pixels   pixel data, @see VMMDevReqMousePointer for the format
 * @param  len  size in bytes of the pixel data
 */
int  hgsmi_update_pointer_shape(struct gen_pool * ctx, u32 flags,
						u32 hot_x, u32 hot_y, u32 width, u32 height,
						u8 *pixels, u32 len)
{
	struct vbva_mouse_pointer_shape *p;
	u32 pixel_len = 0;
	int rc;

	if (flags & VBOX_MOUSE_POINTER_SHAPE) {
		/*
		 * Size of the pointer data:
		 * sizeof (AND mask) + sizeof (XOR_MASK)
		 */
		pixel_len = ((((width + 7) / 8) * height + 3) & ~3)
				 + width * 4 * height;
		if (pixel_len > len)
			return VERR_INVALID_PARAMETER;
		/*
		 * If shape is supplied, then always create the pointer visible.
		 * See comments in 'vboxUpdatePointerShape'
		 */
		flags |= VBOX_MOUSE_POINTER_VISIBLE;
	}
	/* Allocate the IO buffer. */
	p = hgsmi_buffer_alloc(ctx, sizeof(*p) + pixel_len, HGSMI_CH_VBVA,
						VBVA_MOUSE_POINTER_SHAPE);
	if (!p)
		return VERR_NO_MEMORY;
	/* Prepare data to be sent to the host. */
	/* Will be updated by the host. */
	p->result = VINF_SUCCESS;
	/* We have our custom flags in the field */
	p->flags = flags;
	p->hot_x   = hot_x;
	p->hot_y   = hot_y;
	p->width  = width;
	p->height = height;
	if (pixel_len)
		/* Copy the actual pointer data. */
		memcpy (p->data, pixels, pixel_len);
	/* No need to check that the buffer is valid as we have just allocated it. */
	hgsmi_buffer_submit(ctx, p);
	rc = p->result;
	/* Free the IO buffer. */
	hgsmi_buffer_free(ctx, p);
	return rc;
}


/**
 * Report the guest cursor position.  The host may wish to use this information
 * to re-position its own cursor (though this is currently unlikely).  The
 * current host cursor position is returned.
 * @param  ctx             The context containing the heap used.
 * @param  report_position  Are we reporting a position?
 * @param  x                Guest cursor X position.
 * @param  y                Guest cursor Y position.
 * @param  x_host           Host cursor X position is stored here.  Optional.
 * @param  y_host           Host cursor Y position is stored here.  Optional.
 * @returns  iprt status code.
 * @returns  VERR_NO_MEMORY      HGSMI heap allocation failed.
 */
int hgsmi_cursor_position(struct gen_pool * ctx, bool report_position,
						u32 x, u32 y, u32 *x_host, u32 *y_host)
{
	struct vbva_cursor_position *p;

	/* Allocate the IO buffer. */
	p = hgsmi_buffer_alloc(ctx, sizeof(*p), HGSMI_CH_VBVA,
						VBVA_CURSOR_POSITION);
	if (!p)
		return VERR_NO_MEMORY;
	/* Prepare data to be sent to the host. */
	p->report_position = report_position;
	p->x = x;
	p->y = y;
	/* No need to check that the buffer is valid as we have just allocated it. */
	hgsmi_buffer_submit(ctx, p);
	if (x_host)
		*x_host = p->x;
	if (y_host)
		*y_host = p->y;
	/* Free the IO buffer. */
	hgsmi_buffer_free(ctx, p);
	return VINF_SUCCESS;
}


/**
 * @todo Mouse pointer position to be read from VMMDev memory, address of the
 * memory region can be queried from VMMDev via an IOCTL. This VMMDev memory
 * region will contain host information which is needed by the guest.
 *
 * Reading will not cause a switch to the host.
 *
 * Have to take into account:
 *  * synchronization: host must write to the memory only from EMT,
 *    large structures must be read under flag, which tells the host
 *    that the guest is currently reading the memory (OWNER flag?).
 *  * guest writes: may be allocate a page for the host info and make
 *    the page readonly for the guest.
 *  * the information should be available only for additions drivers.
 *  * VMMDev additions driver will inform the host which version of the info
 *    it expects, host must support all versions.
 */
