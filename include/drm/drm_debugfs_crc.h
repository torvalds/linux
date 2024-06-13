/*
 * Copyright © 2016 Collabora Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef __DRM_DEBUGFS_CRC_H__
#define __DRM_DEBUGFS_CRC_H__

#define DRM_MAX_CRC_NR		10

/**
 * struct drm_crtc_crc_entry - entry describing a frame's content
 * @has_frame_counter: whether the source was able to provide a frame number
 * @frame: number of the frame this CRC is about, if @has_frame_counter is true
 * @crc: array of values that characterize the frame
 */
struct drm_crtc_crc_entry {
	bool has_frame_counter;
	uint32_t frame;
	uint32_t crcs[DRM_MAX_CRC_NR];
};

#define DRM_CRC_ENTRIES_NR	128

/**
 * struct drm_crtc_crc - data supporting CRC capture on a given CRTC
 * @lock: protects the fields in this struct
 * @source: name of the currently configured source of CRCs
 * @opened: whether userspace has opened the data file for reading
 * @overflow: whether an overflow occured.
 * @entries: array of entries, with size of %DRM_CRC_ENTRIES_NR
 * @head: head of circular queue
 * @tail: tail of circular queue
 * @values_cnt: number of CRC values per entry, up to %DRM_MAX_CRC_NR
 * @wq: workqueue used to synchronize reading and writing
 */
struct drm_crtc_crc {
	spinlock_t lock;
	const char *source;
	bool opened, overflow;
	struct drm_crtc_crc_entry *entries;
	int head, tail;
	size_t values_cnt;
	wait_queue_head_t wq;
};

#if defined(CONFIG_DEBUG_FS)
int drm_crtc_add_crc_entry(struct drm_crtc *crtc, bool has_frame,
			   uint32_t frame, uint32_t *crcs);
#else
static inline int drm_crtc_add_crc_entry(struct drm_crtc *crtc, bool has_frame,
					 uint32_t frame, uint32_t *crcs)
{
	return -EINVAL;
}
#endif /* defined(CONFIG_DEBUG_FS) */

#endif /* __DRM_DEBUGFS_CRC_H__ */
