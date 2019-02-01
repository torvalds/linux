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

#include "vboxvideo_guest.h"
#include "vbox_err.h"
#include "hgsmi_channels.h"

/*
 * There is a hardware ring buffer in the graphics device video RAM, formerly
 * in the VBox VMMDev PCI memory space.
 * All graphics commands go there serialized by vbva_buffer_begin_update.
 * and vbva_buffer_end_update.
 *
 * free_offset is writing position. data_offset is reading position.
 * free_offset == data_offset means buffer is empty.
 * There must be always gap between data_offset and free_offset when data
 * are in the buffer.
 * Guest only changes free_offset, host changes data_offset.
 */

/* Forward declarations of internal functions. */
static void vbva_buffer_flush(struct gen_pool * ctx);
static void vbva_buffer_place_data_at(struct vbva_buf_context * ctx, const void *p,
						u32 len, u32 offset);
static bool vbva_write(struct vbva_buf_context * ctx,
						struct gen_pool * pHGSMICtx,
						const void *p, u32 len);


static bool vbva_inform_host(struct vbva_buf_context * ctx, struct gen_pool * pHGSMICtx, s32 screen, bool fEnable)
{
	bool fRc = false;

#if 0  /* All callers check this */
	if (ppdev->bHGSMISupported)
#endif
	{
		struct vbva_enable_ex  *pEnable =
			(struct vbva_enable_ex  *)hgsmi_buffer_alloc(pHGSMICtx, sizeof(struct vbva_enable_ex),
						HGSMI_CH_VBVA, VBVA_ENABLE);
		if (pEnable != NULL) {
			pEnable->base.flags  = fEnable ? VBVA_F_ENABLE : VBVA_F_DISABLE;
			pEnable->base.offset = ctx->buffer_offset;
			pEnable->base.result = VERR_NOT_SUPPORTED;
			if (screen >= 0) {
				pEnable->base.flags |= VBVA_F_EXTENDED | VBVA_F_ABSOFFSET;
				pEnable->screen_id    = screen;
			}

			hgsmi_buffer_submit(pHGSMICtx, pEnable);

			if (fEnable)
				fRc = RT_SUCCESS(pEnable->base.result);
			else
				fRc = true;

			hgsmi_buffer_free(pHGSMICtx, pEnable);
		} else {
			// LogFunc(("HGSMIHeapAlloc failed\n"));
		}
	}

	return fRc;
}

/*
 * Public hardware buffer methods.
 */
bool vbva_enable(struct vbva_buf_context * ctx,
						struct gen_pool * pHGSMICtx,
						VBVABUFFER *vbva, s32 screen)
{
	bool fRc = false;

	// LogFlowFunc(("vbva %p\n", vbva));

#if 0  /* All callers check this */
	if (ppdev->bHGSMISupported)
#endif
	{
		// LogFunc(("vbva %p vbva off 0x%x\n", vbva, ctx->buffer_offset));

		vbva->host_flags.host_events      = 0;
		vbva->host_flags.supported_orders = 0;
		vbva->data_offset          = 0;
		vbva->free_offset          = 0;
		memset(vbva->records, 0, sizeof (vbva->records));
		vbva->first_record_index   = 0;
		vbva->free_record_index    = 0;
		vbva->partial_write_tresh = 256;
		vbva->data_len             = ctx->buffer_length - sizeof (VBVABUFFER) + sizeof (vbva->data);

		ctx->buffer_overflow = false;
		ctx->record    = NULL;
		ctx->vbva      = vbva;

		fRc = vbva_inform_host(ctx, pHGSMICtx, screen, true);
	}

	if (!fRc) {
		vbva_disable(ctx, pHGSMICtx, screen);
	}

	return fRc;
}

void vbva_disable(struct vbva_buf_context * ctx,
						struct gen_pool * pHGSMICtx,
						s32 screen)
{
	// LogFlowFunc(("\n"));

	ctx->buffer_overflow = false;
	ctx->record           = NULL;
	ctx->vbva             = NULL;

	vbva_inform_host(ctx, pHGSMICtx, screen, false);
}

bool vbva_buffer_begin_update(struct vbva_buf_context * ctx,
						struct gen_pool * pHGSMICtx)
{
	bool fRc = false;

	// LogFunc(("flags = 0x%08X\n", ctx->vbva? ctx->vbva->host_events: -1));

	if (   ctx->vbva
		&& (ctx->vbva->host_flags.host_events & VBVA_F_MODE_ENABLED)) {
		u32 next;

		WARN_ON_ONCE(!((!ctx->buffer_overflow)));
		WARN_ON_ONCE(!((ctx->record == NULL)));

		next = (ctx->vbva->free_record_index + 1) % VBVA_MAX_RECORDS;

		if (next == ctx->vbva->first_record_index) {
			/* All slots in the records queue are used. */
			vbva_buffer_flush (pHGSMICtx);
		}

		if (next == ctx->vbva->first_record_index) {
			/* Even after flush there is no place. Fail the request. */
			// LogFunc(("no space in the queue of records!!! first %d, last %d\n",
			//          ctx->vbva->first_record_index, ctx->vbva->free_record_index));
		} else {
			/* Initialize the record. */
			VBVARECORD *record = &ctx->vbva->records[ctx->vbva->free_record_index];

			record->len_and_flags = VBVA_F_RECORD_PARTIAL;

			ctx->vbva->free_record_index = next;

			// LogFunc(("next = %d\n", next));

			/* Remember which record we are using. */
			ctx->record = record;

			fRc = true;
		}
	}

	return fRc;
}

void vbva_buffer_end_update(struct vbva_buf_context * ctx)
{
	VBVARECORD *record;

	// LogFunc(("\n"));

	WARN_ON_ONCE(!((ctx->vbva)));

	record = ctx->record;
	WARN_ON_ONCE(!((record && (record->len_and_flags & VBVA_F_RECORD_PARTIAL))));

	/* Mark the record completed. */
	record->len_and_flags &= ~VBVA_F_RECORD_PARTIAL;

	ctx->buffer_overflow = false;
	ctx->record = NULL;
}

/*
 * Private operations.
 */
static u32 vbva_buffer_available (const VBVABUFFER *vbva)
{
	s32 diff = vbva->data_offset - vbva->free_offset;

	return diff > 0? diff: vbva->data_len + diff;
}

static void vbva_buffer_flush(struct gen_pool * ctx)
{
	/* Issue the flush command. */
	VBVAFLUSH  *pFlush =
		(VBVAFLUSH  * )hgsmi_buffer_alloc(ctx, sizeof(VBVAFLUSH), HGSMI_CH_VBVA, VBVA_FLUSH);
	if (pFlush != NULL) {
		pFlush->reserved = 0;

		hgsmi_buffer_submit(ctx, pFlush);

		hgsmi_buffer_free(ctx, pFlush);
	} else {
		// LogFunc(("HGSMIHeapAlloc failed\n"));
	}
}

static void vbva_buffer_place_data_at(struct vbva_buf_context * ctx, const void *p,
						u32 len, u32 offset)
{
	VBVABUFFER *vbva = ctx->vbva;
	u32 bytes_till_boundary = vbva->data_len - offset;
	u8  *dst                 = &vbva->data[offset];
	s32 diff               = len - bytes_till_boundary;

	if (diff <= 0) {
		/* Chunk will not cross buffer boundary. */
		memcpy (dst, p, len);
	} else {
		/* Chunk crosses buffer boundary. */
		memcpy (dst, p, bytes_till_boundary);
		memcpy (&vbva->data[0], (u8 *)p + bytes_till_boundary, diff);
	}
}

static bool vbva_write(struct vbva_buf_context * ctx,
						struct gen_pool * pHGSMICtx,
						const void *p, u32 len)
{
	VBVARECORD *record;
	u32 available;

	u32 cbWritten = 0;

	VBVABUFFER *vbva = ctx->vbva;
	WARN_ON_ONCE(!((vbva)));

	if (!vbva || ctx->buffer_overflow) {
		return false;
	}

	WARN_ON_ONCE(!((vbva->first_record_index != vbva->free_record_index)));

	record = ctx->record;
	WARN_ON_ONCE(!((record && (record->len_and_flags & VBVA_F_RECORD_PARTIAL))));

	// LogFunc(("%d\n", len));

	available = vbva_buffer_available (vbva);

	while (len > 0) {
		u32 chunk = len;

		// LogFunc(("vbva->free_offset %d, record->len_and_flags 0x%08X, available %d, len %d, cbWritten %d\n",
		//             vbva->free_offset, record->len_and_flags, available, len, cbWritten));

		if (chunk >= available) {
			// LogFunc(("1) avail %d, chunk %d\n", available, chunk));

			vbva_buffer_flush (pHGSMICtx);

			available = vbva_buffer_available (vbva);

			if (chunk >= available) {
				// LogFunc(("no place for %d bytes. Only %d bytes available after flush. Going to partial writes.\n",
				//             len, available));

				if (available <= vbva->partial_write_tresh) {
					// LogFunc(("Buffer overflow!!!\n"));
					ctx->buffer_overflow = true;
					WARN_ON_ONCE(!((false)));
					return false;
				}

				chunk = available - vbva->partial_write_tresh;
			}
		}

		WARN_ON_ONCE(!((chunk <= len)));
		WARN_ON_ONCE(!((chunk <= vbva_buffer_available (vbva))));

		vbva_buffer_place_data_at (ctx, (u8 *)p + cbWritten, chunk, vbva->free_offset);

		vbva->free_offset   = (vbva->free_offset + chunk) % vbva->data_len;
		record->len_and_flags += chunk;
		available -= chunk;

		len        -= chunk;
		cbWritten += chunk;
	}

	return true;
}

/*
 * Public writer to the hardware buffer.
 */
bool VBoxVBVAWrite(struct vbva_buf_context * ctx,
						struct gen_pool * pHGSMICtx,
						const void *pv, u32 len)
{
	return vbva_write (ctx, pHGSMICtx, pv, len);
}

bool VBoxVBVAOrderSupported(struct vbva_buf_context * ctx, unsigned code)
{
	VBVABUFFER *vbva = ctx->vbva;

	if (!vbva) {
		return false;
	}

	if (vbva->host_flags.supported_orders & (1 << code)) {
		return true;
	}

	return false;
}

void VBoxVBVASetupBufferContext(struct vbva_buf_context * ctx,
						u32 buffer_offset,
						u32 buffer_length)
{
	ctx->buffer_offset = buffer_offset;
	ctx->buffer_length      = buffer_length;
}
