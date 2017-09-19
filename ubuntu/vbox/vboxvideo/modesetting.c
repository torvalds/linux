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

#include "vboxvideo_guest.h"
#include "vboxvideo_vbe.h"
#include "hgsmi_channels.h"

#ifndef VBOX_GUESTR3XF86MOD
# include "vbox_err.h"
#endif

/**
 * Gets the count of virtual monitors attached to the guest via an HGSMI
 * command
 *
 * @returns the right count on success or 1 on failure.
 * @param  ctx  the context containing the heap to use
 */
u32 VBoxHGSMIGetMonitorCount(struct gen_pool * ctx)
{
	/* Query the configured number of displays. */
	u32 cDisplays = 0;
	hgsmi_query_conf(ctx, VBOX_VBVA_CONF32_MONITOR_COUNT, &cDisplays);
	// LogFunc(("cDisplays = %d\n", cDisplays));
	if (cDisplays == 0 || cDisplays > VBOX_VIDEO_MAX_SCREENS)
		/* Host reported some bad value. Continue in the 1 screen mode. */
		cDisplays = 1;
	return cDisplays;
}


/**
 * Returns the size of the video RAM in bytes.
 *
 * @returns the size
 */
u32 VBoxVideoGetVRAMSize(void)
{
	/** @note A 32bit read on this port returns the VRAM size. */
	return inl(VBE_DISPI_IOPORT_DATA);
}


/**
 * Check whether this hardware allows the display width to have non-multiple-
 * of-eight values.
 *
 * @returns true if any width is allowed, false otherwise.
 */
bool VBoxVideoAnyWidthAllowed(void)
{
	unsigned DispiId;
	outw(VBE_DISPI_INDEX_ID, VBE_DISPI_IOPORT_INDEX);
	outw(VBE_DISPI_ID_ANYX, VBE_DISPI_IOPORT_DATA);
	DispiId = inw(VBE_DISPI_IOPORT_DATA);
	return (DispiId == VBE_DISPI_ID_ANYX);
}


/**
 * Tell the host about how VRAM is divided up between each screen via an HGSMI
 * command.  It is acceptable to specifiy identical data for each screen if
 * they share a single framebuffer.
 *
 * @returns iprt status code, either VERR_NO_MEMORY or the status returned by
 *          @a pfnFill
 * @todo  What was I thinking of with that callback function?  It
 *        would be much simpler to just pass in a structure in normal
 *        memory and copy it.
 * @param  ctx      the context containing the heap to use
 * @param  u32Count  the number of screens we are activating
 * @param  pfnFill   a callback which initialises the VBVAINFOVIEW structures
 *                   for all screens
 * @param  pvData    context data for @a pfnFill
 */
int VBoxHGSMISendViewInfo(struct gen_pool * ctx,
						u32 u32Count,
						PFNHGSMIFILLVIEWINFO pfnFill,
						void *pvData)
{
	int rc;
	/* Issue the screen info command. */
	VBVAINFOVIEW  *pInfo =
		(VBVAINFOVIEW  *)hgsmi_buffer_alloc(ctx, sizeof(VBVAINFOVIEW) * u32Count,
						HGSMI_CH_VBVA, VBVA_INFO_VIEW);
	if (pInfo) {
		rc = pfnFill(pvData, (VBVAINFOVIEW *)pInfo /* lazy bird */, u32Count);
		if (RT_SUCCESS(rc))
			hgsmi_buffer_submit(ctx, pInfo);
		hgsmi_buffer_free(ctx, pInfo);
	} else
		rc = VERR_NO_MEMORY;
	return rc;
}


/**
 * Set a video mode using port registers.  This must be done for the first
 * screen before every HGSMI modeset and also works when HGSM is not enabled.
 * @param  width      the mode width
 * @param  height     the mode height
 * @param  cVirtWidth  the mode pitch
 * @param  bpp        the colour depth of the mode
 * @param  flags      flags for the mode.  These will be or-ed with the
 *                     default _ENABLED flag, so unless you are restoring
 *                     a saved mode or have special requirements you can pass
 *                     zero here.
 * @param  cx          the horizontal panning offset
 * @param  cy          the vertical panning offset
 */
void VBoxVideoSetModeRegisters(u16 width, u16 height,
						u16 cVirtWidth, u16 bpp,
						u16 flags, u16 cx,
						u16 cy)
{
	/* set the mode characteristics */
	outw(VBE_DISPI_INDEX_XRES, VBE_DISPI_IOPORT_INDEX);
	outw(width, VBE_DISPI_IOPORT_DATA);
	outw(VBE_DISPI_INDEX_YRES, VBE_DISPI_IOPORT_INDEX);
	outw(height, VBE_DISPI_IOPORT_DATA);
	outw(VBE_DISPI_INDEX_VIRT_WIDTH, VBE_DISPI_IOPORT_INDEX);
	outw(cVirtWidth, VBE_DISPI_IOPORT_DATA);
	outw(VBE_DISPI_INDEX_BPP, VBE_DISPI_IOPORT_INDEX);
	outw(bpp, VBE_DISPI_IOPORT_DATA);
	/* enable the mode */
	outw(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_IOPORT_INDEX);
	outw(flags | VBE_DISPI_ENABLED, VBE_DISPI_IOPORT_DATA);
	/* Panning registers */
	outw(VBE_DISPI_INDEX_X_OFFSET, VBE_DISPI_IOPORT_INDEX);
	outw(cx, VBE_DISPI_IOPORT_DATA);
	outw(VBE_DISPI_INDEX_Y_OFFSET, VBE_DISPI_IOPORT_INDEX);
	outw(cy, VBE_DISPI_IOPORT_DATA);
	/** @todo read from the port to see if the mode switch was successful */
}


/**
 * Get the video mode for the first screen using the port registers.  All
 * parameters are optional
 * @returns  true if the VBE mode returned is active, false if we are in VGA
 *           mode
 * @note  If anyone else needs additional register values just extend the
 *        function with additional parameters and fix any existing callers.
 * @param  pcWidth      where to store the mode width
 * @param  pcHeight     where to store the mode height
 * @param  pcVirtWidth  where to store the mode pitch
 * @param  pcBPP        where to store the colour depth of the mode
 * @param  pfFlags      where to store the flags for the mode
 */
bool VBoxVideoGetModeRegisters(u16 *pcWidth, u16 *pcHeight,
						u16 *pcVirtWidth, u16 *pcBPP,
						u16 *pfFlags)
{
	u16 flags;

	outw(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_IOPORT_INDEX);
	flags = inw(VBE_DISPI_IOPORT_DATA);
	if (pcWidth) {
		outw(VBE_DISPI_INDEX_XRES, VBE_DISPI_IOPORT_INDEX);
		*pcWidth = inw(VBE_DISPI_IOPORT_DATA);
	}
	if (pcHeight) {
		outw(VBE_DISPI_INDEX_YRES, VBE_DISPI_IOPORT_INDEX);
		*pcHeight = inw(VBE_DISPI_IOPORT_DATA);
	}
	if (pcVirtWidth) {
		outw(VBE_DISPI_INDEX_VIRT_WIDTH, VBE_DISPI_IOPORT_INDEX);
		*pcVirtWidth = inw(VBE_DISPI_IOPORT_DATA);
	}
	if (pcBPP) {
		outw(VBE_DISPI_INDEX_BPP, VBE_DISPI_IOPORT_INDEX);
		*pcBPP = inw(VBE_DISPI_IOPORT_DATA);
	}
	if (pfFlags)
		*pfFlags = flags;
	return (!!(flags & VBE_DISPI_ENABLED));
}


/**
 * Disable our extended graphics mode and go back to VGA mode.
 */
void VBoxVideoDisableVBE(void)
{
	outw(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_IOPORT_INDEX);
	outw(0, VBE_DISPI_IOPORT_DATA);
}


/**
 * Set a video mode via an HGSMI request.  The views must have been
 * initialised first using @a VBoxHGSMISendViewInfo and if the mode is being
 * set on the first display then it must be set first using registers.
 * @param  ctx      The context containing the heap to use.
 * @param  display  the screen number
 * @param  origin_x  the horizontal displacement relative to the first screen
 * @param  origin_y  the vertical displacement relative to the first screen
 * @param  start_offset  the offset of the visible area of the framebuffer
 *                   relative to the framebuffer start
 * @param  pitch   the offset in bytes between the starts of two adjecent
 *                   scan lines in video RAM
 * @param  width    the mode width
 * @param  height   the mode height
 * @param  bpp      the colour depth of the mode
 * @param  flags    flags
 */
void hgsmi_process_display_info(struct gen_pool * ctx,
						u32 display,
						s32  origin_x,
						s32  origin_y,
						u32 start_offset,
						u32 pitch,
						u32 width,
						u32 height,
						u16 bpp,
						u16 flags)
{
	/* Issue the screen info command. */
	VBVAINFOSCREEN  *pScreen =
		(VBVAINFOSCREEN  *)hgsmi_buffer_alloc(ctx, sizeof(VBVAINFOSCREEN),
						HGSMI_CH_VBVA, VBVA_INFO_SCREEN);
	if (pScreen != NULL) {
		pScreen->view_index    = display;
		pScreen->origin_x      = origin_x;
		pScreen->origin_y      = origin_y;
		pScreen->start_offset  = start_offset;
		pScreen->line_size     = pitch;
		pScreen->width        = width;
		pScreen->height       = height;
		pScreen->bits_per_pixel = bpp;
		pScreen->flags        = flags;

		hgsmi_buffer_submit(ctx, pScreen);

		hgsmi_buffer_free(ctx, pScreen);
	} else {
		// LogFunc(("HGSMIHeapAlloc failed\n"));
	}
}


/** Report the rectangle relative to which absolute pointer events should be
 *  expressed.  This information remains valid until the next VBVA resize event
 *  for any screen, at which time it is reset to the bounding rectangle of all
 *  virtual screens.
 * @param  ctx      The context containing the heap to use.
 * @param  origin_x  Upper left X co-ordinate relative to the first screen.
 * @param  origin_y  Upper left Y co-ordinate relative to the first screen.
 * @param  width    Rectangle width.
 * @param  height   Rectangle height.
 * @returns  iprt status code.
 * @returns  VERR_NO_MEMORY      HGSMI heap allocation failed.
 */
int      hgsmi_update_input_mapping(struct gen_pool * ctx, s32  origin_x, s32  origin_y,
						u32 width, u32 height)
{
	int rc;
	struct vbva_report_input_mapping *p;
	// Log(("%s: origin_x=%d, origin_y=%d, width=%u, height=%u\n", __PRETTY_FUNCTION__, (int)origin_x, (int)origin_x,
	//      (unsigned)width, (unsigned)height));

	/* Allocate the IO buffer. */
	p = hgsmi_buffer_alloc(ctx, sizeof(struct vbva_report_input_mapping), HGSMI_CH_VBVA,
						VBVA_REPORT_INPUT_MAPPING);
	if (p) {
		/* Prepare data to be sent to the host. */
		p->x  = origin_x;
		p->y  = origin_y;
		p->cx = width;
		p->cy = height;
		rc = hgsmi_buffer_submit(ctx, p);
		/* Free the IO buffer. */
		hgsmi_buffer_free(ctx, p);
	} else
		rc = VERR_NO_MEMORY;
	// LogFunc(("rc = %d\n", rc));
	return rc;
}


/**
 * Get most recent video mode hints.
 * @param  ctx      the context containing the heap to use
 * @param  screens  the number of screens to query hints for, starting at 0.
 * @param  hints   array of struct vbva_modehint structures for receiving the hints.
 * @returns  iprt status code
 * @returns  VERR_NO_MEMORY      HGSMI heap allocation failed.
 * @returns  VERR_NOT_SUPPORTED  Host does not support this command.
 */
int hgsmi_get_mode_hints(struct gen_pool * ctx,
						unsigned screens, struct vbva_modehint *hints)
{
	int rc;
	struct vbva_query_mode_hints  *pQuery;

	assert_ptr_return(hints, VERR_INVALID_POINTER);
	pQuery = (struct vbva_query_mode_hints  *)hgsmi_buffer_alloc(ctx,
						sizeof(struct vbva_query_mode_hints)
						+  screens * sizeof(struct vbva_modehint),
						HGSMI_CH_VBVA, VBVA_QUERY_MODE_HINTS);
	if (pQuery != NULL) {
		pQuery->hints_queried_count        = screens;
		pQuery->cbHintStructureGuest = sizeof(struct vbva_modehint);
		pQuery->rc                   = VERR_NOT_SUPPORTED;

		hgsmi_buffer_submit(ctx, pQuery);
		rc = pQuery->rc;
		if (RT_SUCCESS(rc))
			memcpy(hints, (void *)(pQuery + 1), screens * sizeof(struct vbva_modehint));

		hgsmi_buffer_free(ctx, pQuery);
	} else {
		// LogFunc(("HGSMIHeapAlloc failed\n"));
		rc = VERR_NO_MEMORY;
	}
	return rc;
}


/**
 * Query the supported flags in VBVAINFOSCREEN::flags.
 *
 * @returns The mask of VBVA_SCREEN_F_* flags or 0 if host does not support the request.
 * @param  ctx  the context containing the heap to use
 */
u16 VBoxHGSMIGetScreenFlags(struct gen_pool * ctx)
{
	u32 flags = 0;
	int rc = hgsmi_query_conf(ctx, VBOX_VBVA_CONF32_SCREEN_FLAGS, &flags);
	// LogFunc(("flags = 0x%x rc %Rrc\n", flags, rc));
	if (RT_FAILURE(rc) || flags > U16_MAX)
		flags = 0;
	return (u16)flags;
}
