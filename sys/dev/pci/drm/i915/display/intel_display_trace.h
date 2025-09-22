/* Public domain. */
#ifndef _INTEL_DISPLAY_TRACE_H
#define _INTEL_DISPLAY_TRACE_H

#include "i915_drv.h"
#include "intel_crtc.h"
#include "intel_display_types.h"
#include "intel_vblank.h"

#define trace_g4x_wm(a, b)
#define trace_intel_cpu_fifo_underrun(a, b)
#define trace_intel_crtc_flip_done(a)
#define trace_intel_crtc_vblank_work_end(a)
#define trace_intel_crtc_vblank_work_start(a)
#define trace_intel_fbc_activate(a)
#define trace_intel_fbc_deactivate(a)
#define trace_intel_fbc_nuke(a)
#define trace_intel_frontbuffer_flush(a, b, c)
#define trace_intel_frontbuffer_invalidate(a, b, c)
#define trace_intel_memory_cxsr(a, b, c)
#define trace_intel_pch_fifo_underrun(a, b)
#define trace_intel_pipe_crc(a, b)
#define trace_intel_pipe_disable(a)
#define trace_intel_pipe_enable(a)
#define trace_intel_pipe_update_end(a, b, c)
#define trace_intel_pipe_update_start(a)
#define trace_intel_pipe_update_vblank_evaded(a)
#define trace_intel_plane_async_flip(a, b, c)
#define trace_intel_plane_disable_arm(a, b)
#define trace_intel_plane_update_arm(a, b)
#define trace_intel_plane_update_noarm(a, b)
#define trace_vlv_fifo_size(a, b, c, d)
#define trace_vlv_wm(a, b)

#endif
