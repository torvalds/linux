/*
 * Copyright (C) 2006-2019 Oracle Corporation
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

#ifndef VBOX_INCLUDED_Graphics_VBoxVideoGuest_h
#define VBOX_INCLUDED_Graphics_VBoxVideoGuest_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "vbox_err.h"
#include "vbox_drv.h"
#include "vboxvideo.h"


/**
 * Structure grouping the context needed for sending graphics acceleration
 * information to the host via VBVA.  Each screen has its own VBVA buffer.
 */
typedef struct vbva_buf_context {
	/** Offset of the buffer in the VRAM section for the screen */
	u32    buffer_offset;
	/** Length of the buffer in bytes */
	u32    buffer_length;
	/** This flag is set if we wrote to the buffer faster than the host could
	 * read it. */
	bool        buffer_overflow;
	/** The VBVA record that we are currently preparing for the host, NULL if
	 * none. */
	struct VBVARECORD *record;
	/** Pointer to the VBVA buffer mapped into the current address space.  Will
	 * be NULL if VBVA is not enabled. */
	struct VBVABUFFER *vbva;
} vbva_buf_context, *PVBVABUFFERCONTEXT;

/** @name base HGSMI APIs
 * @{ */

bool     VBoxHGSMIIsSupported(void);
void     VBoxHGSMIGetBaseMappingInfo(u32 cbVRAM,
						u32 *poffVRAMBaseMapping,
						u32 *pcbMapping,
						u32 *poffGuestHeapMemory,
						u32 *pcbGuestHeapMemory,
						u32 *poffHostFlags);
int      hgsmi_report_flags_location(struct gen_pool * ctx,
						u32 location);
int      hgsmi_send_caps_info(struct gen_pool * ctx,
						u32 caps);
void     VBoxHGSMIGetHostAreaMapping(struct gen_pool * ctx,
						u32 cbVRAM,
						u32 offVRAMBaseMapping,
						u32 *poffVRAMHostArea,
						u32 *pcbHostArea);
int      VBoxHGSMISendHostCtxInfo(struct gen_pool * ctx,
						u32 offVRAMFlagsLocation,
						u32 caps,
						u32 offVRAMHostArea,
						u32 cbHostArea);
int      hgsmi_query_conf(struct gen_pool * ctx,
						u32 index, u32 *value_ret);
int      VBoxQueryConfHGSMIDef(struct gen_pool * ctx,
						u32 index, u32 u32DefValue, u32 *value_ret);
int      hgsmi_update_pointer_shape(struct gen_pool * ctx,
						u32 flags,
						u32 hot_x,
						u32 hot_y,
						u32 width,
						u32 height,
						u8 *pixels,
						u32 len);
int      hgsmi_cursor_position(struct gen_pool * ctx, bool report_position, u32 x, u32 y,
						u32 *x_host, u32 *y_host);

/** @}  */

/** @name VBVA APIs
 * @{ */
bool vbva_enable(struct vbva_buf_context * ctx,
						struct gen_pool * pHGSMICtx,
						struct VBVABUFFER *vbva, s32 screen);
void vbva_disable(struct vbva_buf_context * ctx,
						struct gen_pool * pHGSMICtx,
						s32 screen);
bool vbva_buffer_begin_update(struct vbva_buf_context * ctx,
						struct gen_pool * pHGSMICtx);
void vbva_buffer_end_update(struct vbva_buf_context * ctx);
bool VBoxVBVAWrite(struct vbva_buf_context * ctx,
						struct gen_pool * pHGSMICtx,
						const void *pv, u32 len);
bool VBoxVBVAOrderSupported(struct vbva_buf_context * ctx, unsigned code);
void VBoxVBVASetupBufferContext(struct vbva_buf_context * ctx,
						u32 buffer_offset,
						u32 buffer_length);

/** @}  */

/** @name Modesetting APIs
 * @{ */

u32 VBoxHGSMIGetMonitorCount(struct gen_pool * ctx);
bool     VBoxVGACfgAvailable(void);
bool     VBoxVGACfgQuery(u16 u16Id, u32 *pu32Value, u32 u32DefValue);
u32 VBoxVideoGetVRAMSize(void);
bool     VBoxVideoAnyWidthAllowed(void);
u16 VBoxHGSMIGetScreenFlags(struct gen_pool * ctx);

struct VBVAINFOVIEW;
/**
 * Callback funtion called from @a VBoxHGSMISendViewInfo to initialise
 * the @a VBVAINFOVIEW structure for each screen.
 *
 * @returns  iprt status code
 * @param  pvData  context data for the callback, passed to @a
 *                 VBoxHGSMISendViewInfo along with the callback
 * @param  pInfo   array of @a VBVAINFOVIEW structures to be filled in
 * @todo  explicitly pass the array size
 */
typedef int FNHGSMIFILLVIEWINFO(void *pvData,
						struct VBVAINFOVIEW *pInfo,
						u32 cViews);
/** Pointer to a FNHGSMIFILLVIEWINFO callback */
typedef FNHGSMIFILLVIEWINFO *PFNHGSMIFILLVIEWINFO;

int      VBoxHGSMISendViewInfo(struct gen_pool * ctx,
						u32 u32Count,
						PFNHGSMIFILLVIEWINFO pfnFill,
						void *pvData);
void     VBoxVideoSetModeRegisters(u16 width, u16 height,
						u16 cVirtWidth, u16 bpp,
						u16 flags,
						u16 cx, u16 cy);
bool     VBoxVideoGetModeRegisters(u16 *pcWidth,
						u16 *pcHeight,
						u16 *pcVirtWidth,
						u16 *pcBPP,
						u16 *pfFlags);
void     VBoxVideoDisableVBE(void);
void     hgsmi_process_display_info(struct gen_pool * ctx,
						u32 display,
						s32  origin_x,
						s32  origin_y,
						u32 start_offset,
						u32 pitch,
						u32 width,
						u32 height,
						u16 bpp,
						u16 flags);
int      hgsmi_update_input_mapping(struct gen_pool * ctx, s32  origin_x, s32  origin_y,
						u32 width, u32 height);
int hgsmi_get_mode_hints(struct gen_pool * ctx,
						unsigned screens, struct vbva_modehint *hints);

/** @}  */


#endif /* !VBOX_INCLUDED_Graphics_VBoxVideoGuest_h */

