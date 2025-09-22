// SPDX-License-Identifier: MIT
/*
 * Copyright © 2014 Intel Corporation
 */

#include "gem/i915_gem_lmem.h"

#include "gen8_engine_cs.h"
#include "i915_drv.h"
#include "i915_perf.h"
#include "i915_reg.h"
#include "intel_context.h"
#include "intel_engine.h"
#include "intel_engine_regs.h"
#include "intel_gpu_commands.h"
#include "intel_gt.h"
#include "intel_gt_regs.h"
#include "intel_lrc.h"
#include "intel_lrc_reg.h"
#include "intel_ring.h"
#include "shmem_utils.h"

/*
 * The per-platform tables are u8-encoded in @data. Decode @data and set the
 * addresses' offset and commands in @regs. The following encoding is used
 * for each byte. There are 2 steps: decoding commands and decoding addresses.
 *
 * Commands:
 * [7]: create NOPs - number of NOPs are set in lower bits
 * [6]: When creating MI_LOAD_REGISTER_IMM command, allow to set
 *      MI_LRI_FORCE_POSTED
 * [5:0]: Number of NOPs or registers to set values to in case of
 *        MI_LOAD_REGISTER_IMM
 *
 * Addresses: these are decoded after a MI_LOAD_REGISTER_IMM command by "count"
 * number of registers. They are set by using the REG/REG16 macros: the former
 * is used for offsets smaller than 0x200 while the latter is for values bigger
 * than that. Those macros already set all the bits documented below correctly:
 *
 * [7]: When a register offset needs more than 6 bits, use additional bytes, to
 *      follow, for the lower bits
 * [6:0]: Register offset, without considering the engine base.
 *
 * This function only tweaks the commands and register offsets. Values are not
 * filled out.
 */
static void set_offsets(u32 *regs,
			const u8 *data,
			const struct intel_engine_cs *engine,
			bool close)
#define NOP(x) (BIT(7) | (x))
#define LRI(count, flags) ((flags) << 6 | (count) | BUILD_BUG_ON_ZERO(count >= BIT(6)))
#define POSTED BIT(0)
#define REG(x) (((x) >> 2) | BUILD_BUG_ON_ZERO(x >= 0x200))
#define REG16(x) \
	(((x) >> 9) | BIT(7) | BUILD_BUG_ON_ZERO(x >= 0x10000)), \
	(((x) >> 2) & 0x7f)
#define END 0
{
	const u32 base = engine->mmio_base;

	while (*data) {
		u8 count, flags;

		if (*data & BIT(7)) { /* skip */
			count = *data++ & ~BIT(7);
			regs += count;
			continue;
		}

		count = *data & 0x3f;
		flags = *data >> 6;
		data++;

		*regs = MI_LOAD_REGISTER_IMM(count);
		if (flags & POSTED)
			*regs |= MI_LRI_FORCE_POSTED;
		if (GRAPHICS_VER(engine->i915) >= 11)
			*regs |= MI_LRI_LRM_CS_MMIO;
		regs++;

		GEM_BUG_ON(!count);
		do {
			u32 offset = 0;
			u8 v;

			do {
				v = *data++;
				offset <<= 7;
				offset |= v & ~BIT(7);
			} while (v & BIT(7));

			regs[0] = base + (offset << 2);
			regs += 2;
		} while (--count);
	}

	if (close) {
		/* Close the batch; used mainly by live_lrc_layout() */
		*regs = MI_BATCH_BUFFER_END;
		if (GRAPHICS_VER(engine->i915) >= 11)
			*regs |= BIT(0);
	}
}

static const u8 gen8_xcs_offsets[] = {
	NOP(1),
	LRI(11, 0),
	REG16(0x244),
	REG(0x034),
	REG(0x030),
	REG(0x038),
	REG(0x03c),
	REG(0x168),
	REG(0x140),
	REG(0x110),
	REG(0x11c),
	REG(0x114),
	REG(0x118),

	NOP(9),
	LRI(9, 0),
	REG16(0x3a8),
	REG16(0x28c),
	REG16(0x288),
	REG16(0x284),
	REG16(0x280),
	REG16(0x27c),
	REG16(0x278),
	REG16(0x274),
	REG16(0x270),

	NOP(13),
	LRI(2, 0),
	REG16(0x200),
	REG(0x028),

	END
};

static const u8 gen9_xcs_offsets[] = {
	NOP(1),
	LRI(14, POSTED),
	REG16(0x244),
	REG(0x034),
	REG(0x030),
	REG(0x038),
	REG(0x03c),
	REG(0x168),
	REG(0x140),
	REG(0x110),
	REG(0x11c),
	REG(0x114),
	REG(0x118),
	REG(0x1c0),
	REG(0x1c4),
	REG(0x1c8),

	NOP(3),
	LRI(9, POSTED),
	REG16(0x3a8),
	REG16(0x28c),
	REG16(0x288),
	REG16(0x284),
	REG16(0x280),
	REG16(0x27c),
	REG16(0x278),
	REG16(0x274),
	REG16(0x270),

	NOP(13),
	LRI(1, POSTED),
	REG16(0x200),

	NOP(13),
	LRI(44, POSTED),
	REG(0x028),
	REG(0x09c),
	REG(0x0c0),
	REG(0x178),
	REG(0x17c),
	REG16(0x358),
	REG(0x170),
	REG(0x150),
	REG(0x154),
	REG(0x158),
	REG16(0x41c),
	REG16(0x600),
	REG16(0x604),
	REG16(0x608),
	REG16(0x60c),
	REG16(0x610),
	REG16(0x614),
	REG16(0x618),
	REG16(0x61c),
	REG16(0x620),
	REG16(0x624),
	REG16(0x628),
	REG16(0x62c),
	REG16(0x630),
	REG16(0x634),
	REG16(0x638),
	REG16(0x63c),
	REG16(0x640),
	REG16(0x644),
	REG16(0x648),
	REG16(0x64c),
	REG16(0x650),
	REG16(0x654),
	REG16(0x658),
	REG16(0x65c),
	REG16(0x660),
	REG16(0x664),
	REG16(0x668),
	REG16(0x66c),
	REG16(0x670),
	REG16(0x674),
	REG16(0x678),
	REG16(0x67c),
	REG(0x068),

	END
};

static const u8 gen12_xcs_offsets[] = {
	NOP(1),
	LRI(13, POSTED),
	REG16(0x244),
	REG(0x034),
	REG(0x030),
	REG(0x038),
	REG(0x03c),
	REG(0x168),
	REG(0x140),
	REG(0x110),
	REG(0x1c0),
	REG(0x1c4),
	REG(0x1c8),
	REG(0x180),
	REG16(0x2b4),

	NOP(5),
	LRI(9, POSTED),
	REG16(0x3a8),
	REG16(0x28c),
	REG16(0x288),
	REG16(0x284),
	REG16(0x280),
	REG16(0x27c),
	REG16(0x278),
	REG16(0x274),
	REG16(0x270),

	END
};

static const u8 dg2_xcs_offsets[] = {
	NOP(1),
	LRI(15, POSTED),
	REG16(0x244),
	REG(0x034),
	REG(0x030),
	REG(0x038),
	REG(0x03c),
	REG(0x168),
	REG(0x140),
	REG(0x110),
	REG(0x1c0),
	REG(0x1c4),
	REG(0x1c8),
	REG(0x180),
	REG16(0x2b4),
	REG(0x120),
	REG(0x124),

	NOP(1),
	LRI(9, POSTED),
	REG16(0x3a8),
	REG16(0x28c),
	REG16(0x288),
	REG16(0x284),
	REG16(0x280),
	REG16(0x27c),
	REG16(0x278),
	REG16(0x274),
	REG16(0x270),

	END
};

static const u8 gen8_rcs_offsets[] = {
	NOP(1),
	LRI(14, POSTED),
	REG16(0x244),
	REG(0x034),
	REG(0x030),
	REG(0x038),
	REG(0x03c),
	REG(0x168),
	REG(0x140),
	REG(0x110),
	REG(0x11c),
	REG(0x114),
	REG(0x118),
	REG(0x1c0),
	REG(0x1c4),
	REG(0x1c8),

	NOP(3),
	LRI(9, POSTED),
	REG16(0x3a8),
	REG16(0x28c),
	REG16(0x288),
	REG16(0x284),
	REG16(0x280),
	REG16(0x27c),
	REG16(0x278),
	REG16(0x274),
	REG16(0x270),

	NOP(13),
	LRI(1, 0),
	REG(0x0c8),

	END
};

static const u8 gen9_rcs_offsets[] = {
	NOP(1),
	LRI(14, POSTED),
	REG16(0x244),
	REG(0x34),
	REG(0x30),
	REG(0x38),
	REG(0x3c),
	REG(0x168),
	REG(0x140),
	REG(0x110),
	REG(0x11c),
	REG(0x114),
	REG(0x118),
	REG(0x1c0),
	REG(0x1c4),
	REG(0x1c8),

	NOP(3),
	LRI(9, POSTED),
	REG16(0x3a8),
	REG16(0x28c),
	REG16(0x288),
	REG16(0x284),
	REG16(0x280),
	REG16(0x27c),
	REG16(0x278),
	REG16(0x274),
	REG16(0x270),

	NOP(13),
	LRI(1, 0),
	REG(0xc8),

	NOP(13),
	LRI(44, POSTED),
	REG(0x28),
	REG(0x9c),
	REG(0xc0),
	REG(0x178),
	REG(0x17c),
	REG16(0x358),
	REG(0x170),
	REG(0x150),
	REG(0x154),
	REG(0x158),
	REG16(0x41c),
	REG16(0x600),
	REG16(0x604),
	REG16(0x608),
	REG16(0x60c),
	REG16(0x610),
	REG16(0x614),
	REG16(0x618),
	REG16(0x61c),
	REG16(0x620),
	REG16(0x624),
	REG16(0x628),
	REG16(0x62c),
	REG16(0x630),
	REG16(0x634),
	REG16(0x638),
	REG16(0x63c),
	REG16(0x640),
	REG16(0x644),
	REG16(0x648),
	REG16(0x64c),
	REG16(0x650),
	REG16(0x654),
	REG16(0x658),
	REG16(0x65c),
	REG16(0x660),
	REG16(0x664),
	REG16(0x668),
	REG16(0x66c),
	REG16(0x670),
	REG16(0x674),
	REG16(0x678),
	REG16(0x67c),
	REG(0x68),

	END
};

static const u8 gen11_rcs_offsets[] = {
	NOP(1),
	LRI(15, POSTED),
	REG16(0x244),
	REG(0x034),
	REG(0x030),
	REG(0x038),
	REG(0x03c),
	REG(0x168),
	REG(0x140),
	REG(0x110),
	REG(0x11c),
	REG(0x114),
	REG(0x118),
	REG(0x1c0),
	REG(0x1c4),
	REG(0x1c8),
	REG(0x180),

	NOP(1),
	LRI(9, POSTED),
	REG16(0x3a8),
	REG16(0x28c),
	REG16(0x288),
	REG16(0x284),
	REG16(0x280),
	REG16(0x27c),
	REG16(0x278),
	REG16(0x274),
	REG16(0x270),

	LRI(1, POSTED),
	REG(0x1b0),

	NOP(10),
	LRI(1, 0),
	REG(0x0c8),

	END
};

static const u8 gen12_rcs_offsets[] = {
	NOP(1),
	LRI(13, POSTED),
	REG16(0x244),
	REG(0x034),
	REG(0x030),
	REG(0x038),
	REG(0x03c),
	REG(0x168),
	REG(0x140),
	REG(0x110),
	REG(0x1c0),
	REG(0x1c4),
	REG(0x1c8),
	REG(0x180),
	REG16(0x2b4),

	NOP(5),
	LRI(9, POSTED),
	REG16(0x3a8),
	REG16(0x28c),
	REG16(0x288),
	REG16(0x284),
	REG16(0x280),
	REG16(0x27c),
	REG16(0x278),
	REG16(0x274),
	REG16(0x270),

	LRI(3, POSTED),
	REG(0x1b0),
	REG16(0x5a8),
	REG16(0x5ac),

	NOP(6),
	LRI(1, 0),
	REG(0x0c8),
	NOP(3 + 9 + 1),

	LRI(51, POSTED),
	REG16(0x588),
	REG16(0x588),
	REG16(0x588),
	REG16(0x588),
	REG16(0x588),
	REG16(0x588),
	REG(0x028),
	REG(0x09c),
	REG(0x0c0),
	REG(0x178),
	REG(0x17c),
	REG16(0x358),
	REG(0x170),
	REG(0x150),
	REG(0x154),
	REG(0x158),
	REG16(0x41c),
	REG16(0x600),
	REG16(0x604),
	REG16(0x608),
	REG16(0x60c),
	REG16(0x610),
	REG16(0x614),
	REG16(0x618),
	REG16(0x61c),
	REG16(0x620),
	REG16(0x624),
	REG16(0x628),
	REG16(0x62c),
	REG16(0x630),
	REG16(0x634),
	REG16(0x638),
	REG16(0x63c),
	REG16(0x640),
	REG16(0x644),
	REG16(0x648),
	REG16(0x64c),
	REG16(0x650),
	REG16(0x654),
	REG16(0x658),
	REG16(0x65c),
	REG16(0x660),
	REG16(0x664),
	REG16(0x668),
	REG16(0x66c),
	REG16(0x670),
	REG16(0x674),
	REG16(0x678),
	REG16(0x67c),
	REG(0x068),
	REG(0x084),
	NOP(1),

	END
};

static const u8 dg2_rcs_offsets[] = {
	NOP(1),
	LRI(15, POSTED),
	REG16(0x244),
	REG(0x034),
	REG(0x030),
	REG(0x038),
	REG(0x03c),
	REG(0x168),
	REG(0x140),
	REG(0x110),
	REG(0x1c0),
	REG(0x1c4),
	REG(0x1c8),
	REG(0x180),
	REG16(0x2b4),
	REG(0x120),
	REG(0x124),

	NOP(1),
	LRI(9, POSTED),
	REG16(0x3a8),
	REG16(0x28c),
	REG16(0x288),
	REG16(0x284),
	REG16(0x280),
	REG16(0x27c),
	REG16(0x278),
	REG16(0x274),
	REG16(0x270),

	LRI(3, POSTED),
	REG(0x1b0),
	REG16(0x5a8),
	REG16(0x5ac),

	NOP(6),
	LRI(1, 0),
	REG(0x0c8),

	END
};

static const u8 mtl_rcs_offsets[] = {
	NOP(1),
	LRI(15, POSTED),
	REG16(0x244),
	REG(0x034),
	REG(0x030),
	REG(0x038),
	REG(0x03c),
	REG(0x168),
	REG(0x140),
	REG(0x110),
	REG(0x1c0),
	REG(0x1c4),
	REG(0x1c8),
	REG(0x180),
	REG16(0x2b4),
	REG(0x120),
	REG(0x124),

	NOP(1),
	LRI(9, POSTED),
	REG16(0x3a8),
	REG16(0x28c),
	REG16(0x288),
	REG16(0x284),
	REG16(0x280),
	REG16(0x27c),
	REG16(0x278),
	REG16(0x274),
	REG16(0x270),

	NOP(2),
	LRI(2, POSTED),
	REG16(0x5a8),
	REG16(0x5ac),

	NOP(6),
	LRI(1, 0),
	REG(0x0c8),

	END
};

#undef END
#undef REG16
#undef REG
#undef LRI
#undef NOP

static const u8 *reg_offsets(const struct intel_engine_cs *engine)
{
	/*
	 * The gen12+ lists only have the registers we program in the basic
	 * default state. We rely on the context image using relative
	 * addressing to automatic fixup the register state between the
	 * physical engines for virtual engine.
	 */
	GEM_BUG_ON(GRAPHICS_VER(engine->i915) >= 12 &&
		   !intel_engine_has_relative_mmio(engine));

	if (engine->flags & I915_ENGINE_HAS_RCS_REG_STATE) {
		if (GRAPHICS_VER_FULL(engine->i915) >= IP_VER(12, 70))
			return mtl_rcs_offsets;
		else if (GRAPHICS_VER_FULL(engine->i915) >= IP_VER(12, 55))
			return dg2_rcs_offsets;
		else if (GRAPHICS_VER(engine->i915) >= 12)
			return gen12_rcs_offsets;
		else if (GRAPHICS_VER(engine->i915) >= 11)
			return gen11_rcs_offsets;
		else if (GRAPHICS_VER(engine->i915) >= 9)
			return gen9_rcs_offsets;
		else
			return gen8_rcs_offsets;
	} else {
		if (GRAPHICS_VER_FULL(engine->i915) >= IP_VER(12, 55))
			return dg2_xcs_offsets;
		else if (GRAPHICS_VER(engine->i915) >= 12)
			return gen12_xcs_offsets;
		else if (GRAPHICS_VER(engine->i915) >= 9)
			return gen9_xcs_offsets;
		else
			return gen8_xcs_offsets;
	}
}

static int lrc_ring_mi_mode(const struct intel_engine_cs *engine)
{
	if (GRAPHICS_VER_FULL(engine->i915) >= IP_VER(12, 55))
		return 0x70;
	else if (GRAPHICS_VER(engine->i915) >= 12)
		return 0x60;
	else if (GRAPHICS_VER(engine->i915) >= 9)
		return 0x54;
	else if (engine->class == RENDER_CLASS)
		return 0x58;
	else
		return -1;
}

static int lrc_ring_bb_offset(const struct intel_engine_cs *engine)
{
	if (GRAPHICS_VER_FULL(engine->i915) >= IP_VER(12, 55))
		return 0x80;
	else if (GRAPHICS_VER(engine->i915) >= 12)
		return 0x70;
	else if (GRAPHICS_VER(engine->i915) >= 9)
		return 0x64;
	else if (GRAPHICS_VER(engine->i915) >= 8 &&
		 engine->class == RENDER_CLASS)
		return 0xc4;
	else
		return -1;
}

static int lrc_ring_gpr0(const struct intel_engine_cs *engine)
{
	if (GRAPHICS_VER_FULL(engine->i915) >= IP_VER(12, 55))
		return 0x84;
	else if (GRAPHICS_VER(engine->i915) >= 12)
		return 0x74;
	else if (GRAPHICS_VER(engine->i915) >= 9)
		return 0x68;
	else if (engine->class == RENDER_CLASS)
		return 0xd8;
	else
		return -1;
}

static int lrc_ring_wa_bb_per_ctx(const struct intel_engine_cs *engine)
{
	if (GRAPHICS_VER(engine->i915) >= 12)
		return 0x12;
	else if (GRAPHICS_VER(engine->i915) >= 9 || engine->class == RENDER_CLASS)
		return 0x18;
	else
		return -1;
}

static int lrc_ring_indirect_ptr(const struct intel_engine_cs *engine)
{
	int x;

	x = lrc_ring_wa_bb_per_ctx(engine);
	if (x < 0)
		return x;

	return x + 2;
}

static int lrc_ring_indirect_offset(const struct intel_engine_cs *engine)
{
	int x;

	x = lrc_ring_indirect_ptr(engine);
	if (x < 0)
		return x;

	return x + 2;
}

static int lrc_ring_cmd_buf_cctl(const struct intel_engine_cs *engine)
{

	if (GRAPHICS_VER_FULL(engine->i915) >= IP_VER(12, 55))
		/*
		 * Note that the CSFE context has a dummy slot for CMD_BUF_CCTL
		 * simply to match the RCS context image layout.
		 */
		return 0xc6;
	else if (engine->class != RENDER_CLASS)
		return -1;
	else if (GRAPHICS_VER(engine->i915) >= 12)
		return 0xb6;
	else if (GRAPHICS_VER(engine->i915) >= 11)
		return 0xaa;
	else
		return -1;
}

static u32
lrc_ring_indirect_offset_default(const struct intel_engine_cs *engine)
{
	if (GRAPHICS_VER(engine->i915) >= 12)
		return GEN12_CTX_RCS_INDIRECT_CTX_OFFSET_DEFAULT;
	else if (GRAPHICS_VER(engine->i915) >= 11)
		return GEN11_CTX_RCS_INDIRECT_CTX_OFFSET_DEFAULT;
	else if (GRAPHICS_VER(engine->i915) >= 9)
		return GEN9_CTX_RCS_INDIRECT_CTX_OFFSET_DEFAULT;
	else if (GRAPHICS_VER(engine->i915) >= 8)
		return GEN8_CTX_RCS_INDIRECT_CTX_OFFSET_DEFAULT;

	GEM_BUG_ON(GRAPHICS_VER(engine->i915) < 8);

	return 0;
}

static void
lrc_setup_bb_per_ctx(u32 *regs,
		     const struct intel_engine_cs *engine,
		     u32 ctx_bb_ggtt_addr)
{
	GEM_BUG_ON(lrc_ring_wa_bb_per_ctx(engine) == -1);
	regs[lrc_ring_wa_bb_per_ctx(engine) + 1] =
		ctx_bb_ggtt_addr |
		PER_CTX_BB_FORCE |
		PER_CTX_BB_VALID;
}

static void
lrc_setup_indirect_ctx(u32 *regs,
		       const struct intel_engine_cs *engine,
		       u32 ctx_bb_ggtt_addr,
		       u32 size)
{
	GEM_BUG_ON(!size);
	GEM_BUG_ON(!IS_ALIGNED(size, CACHELINE_BYTES));
	GEM_BUG_ON(lrc_ring_indirect_ptr(engine) == -1);
	regs[lrc_ring_indirect_ptr(engine) + 1] =
		ctx_bb_ggtt_addr | (size / CACHELINE_BYTES);

	GEM_BUG_ON(lrc_ring_indirect_offset(engine) == -1);
	regs[lrc_ring_indirect_offset(engine) + 1] =
		lrc_ring_indirect_offset_default(engine) << 6;
}

static bool ctx_needs_runalone(const struct intel_context *ce)
{
	struct i915_gem_context *gem_ctx;
	bool ctx_is_protected = false;

	/*
	 * On MTL and newer platforms, protected contexts require setting
	 * the LRC run-alone bit or else the encryption will not happen.
	 */
	if (GRAPHICS_VER_FULL(ce->engine->i915) >= IP_VER(12, 70) &&
	    (ce->engine->class == COMPUTE_CLASS || ce->engine->class == RENDER_CLASS)) {
		rcu_read_lock();
		gem_ctx = rcu_dereference(ce->gem_context);
		if (gem_ctx)
			ctx_is_protected = gem_ctx->uses_protected_content;
		rcu_read_unlock();
	}

	return ctx_is_protected;
}

static void init_common_regs(u32 * const regs,
			     const struct intel_context *ce,
			     const struct intel_engine_cs *engine,
			     bool inhibit)
{
	u32 ctl;
	int loc;

	ctl = _MASKED_BIT_ENABLE(CTX_CTRL_INHIBIT_SYN_CTX_SWITCH);
	ctl |= _MASKED_BIT_DISABLE(CTX_CTRL_ENGINE_CTX_RESTORE_INHIBIT);
	if (inhibit)
		ctl |= CTX_CTRL_ENGINE_CTX_RESTORE_INHIBIT;
	if (GRAPHICS_VER(engine->i915) < 11)
		ctl |= _MASKED_BIT_DISABLE(CTX_CTRL_ENGINE_CTX_SAVE_INHIBIT |
					   CTX_CTRL_RS_CTX_ENABLE);
	if (ctx_needs_runalone(ce))
		ctl |= _MASKED_BIT_ENABLE(GEN12_CTX_CTRL_RUNALONE_MODE);
	regs[CTX_CONTEXT_CONTROL] = ctl;

	regs[CTX_TIMESTAMP] = ce->stats.runtime.last;

	loc = lrc_ring_bb_offset(engine);
	if (loc != -1)
		regs[loc + 1] = 0;
}

static void init_wa_bb_regs(u32 * const regs,
			    const struct intel_engine_cs *engine)
{
	const struct i915_ctx_workarounds * const wa_ctx = &engine->wa_ctx;

	if (wa_ctx->per_ctx.size) {
		const u32 ggtt_offset = i915_ggtt_offset(wa_ctx->vma);

		GEM_BUG_ON(lrc_ring_wa_bb_per_ctx(engine) == -1);
		regs[lrc_ring_wa_bb_per_ctx(engine) + 1] =
			(ggtt_offset + wa_ctx->per_ctx.offset) | 0x01;
	}

	if (wa_ctx->indirect_ctx.size) {
		lrc_setup_indirect_ctx(regs, engine,
				       i915_ggtt_offset(wa_ctx->vma) +
				       wa_ctx->indirect_ctx.offset,
				       wa_ctx->indirect_ctx.size);
	}
}

static void init_ppgtt_regs(u32 *regs, const struct i915_ppgtt *ppgtt)
{
	if (i915_vm_is_4lvl(&ppgtt->vm)) {
		/* 64b PPGTT (48bit canonical)
		 * PDP0_DESCRIPTOR contains the base address to PML4 and
		 * other PDP Descriptors are ignored.
		 */
		ASSIGN_CTX_PML4(ppgtt, regs);
	} else {
		ASSIGN_CTX_PDP(ppgtt, regs, 3);
		ASSIGN_CTX_PDP(ppgtt, regs, 2);
		ASSIGN_CTX_PDP(ppgtt, regs, 1);
		ASSIGN_CTX_PDP(ppgtt, regs, 0);
	}
}

static struct i915_ppgtt *vm_alias(struct i915_address_space *vm)
{
	if (i915_is_ggtt(vm))
		return i915_vm_to_ggtt(vm)->alias;
	else
		return i915_vm_to_ppgtt(vm);
}

static void __reset_stop_ring(u32 *regs, const struct intel_engine_cs *engine)
{
	int x;

	x = lrc_ring_mi_mode(engine);
	if (x != -1) {
		regs[x + 1] &= ~STOP_RING;
		regs[x + 1] |= STOP_RING << 16;
	}
}

static void __lrc_init_regs(u32 *regs,
			    const struct intel_context *ce,
			    const struct intel_engine_cs *engine,
			    bool inhibit)
{
	/*
	 * A context is actually a big batch buffer with several
	 * MI_LOAD_REGISTER_IMM commands followed by (reg, value) pairs. The
	 * values we are setting here are only for the first context restore:
	 * on a subsequent save, the GPU will recreate this batchbuffer with new
	 * values (including all the missing MI_LOAD_REGISTER_IMM commands that
	 * we are not initializing here).
	 *
	 * Must keep consistent with virtual_update_register_offsets().
	 */

	if (inhibit)
		memset(regs, 0, PAGE_SIZE);

	set_offsets(regs, reg_offsets(engine), engine, inhibit);

	init_common_regs(regs, ce, engine, inhibit);
	init_ppgtt_regs(regs, vm_alias(ce->vm));

	init_wa_bb_regs(regs, engine);

	__reset_stop_ring(regs, engine);
}

void lrc_init_regs(const struct intel_context *ce,
		   const struct intel_engine_cs *engine,
		   bool inhibit)
{
	__lrc_init_regs(ce->lrc_reg_state, ce, engine, inhibit);
}

void lrc_reset_regs(const struct intel_context *ce,
		    const struct intel_engine_cs *engine)
{
	__reset_stop_ring(ce->lrc_reg_state, engine);
}

static void
set_redzone(void *vaddr, const struct intel_engine_cs *engine)
{
	if (!IS_ENABLED(CONFIG_DRM_I915_DEBUG_GEM))
		return;

	vaddr += engine->context_size;

	memset(vaddr, CONTEXT_REDZONE, I915_GTT_PAGE_SIZE);
}

static void
check_redzone(const void *vaddr, const struct intel_engine_cs *engine)
{
	if (!IS_ENABLED(CONFIG_DRM_I915_DEBUG_GEM))
		return;

	vaddr += engine->context_size;

	if (memchr_inv(vaddr, CONTEXT_REDZONE, I915_GTT_PAGE_SIZE))
		drm_err_once(&engine->i915->drm,
			     "%s context redzone overwritten!\n",
			     engine->name);
}

static u32 context_wa_bb_offset(const struct intel_context *ce)
{
	return PAGE_SIZE * ce->wa_bb_page;
}

/*
 * per_ctx below determines which WABB section is used.
 * When true, the function returns the location of the
 * PER_CTX_BB.  When false, the function returns the
 * location of the INDIRECT_CTX.
 */
static u32 *context_wabb(const struct intel_context *ce, bool per_ctx)
{
	void *ptr;

	GEM_BUG_ON(!ce->wa_bb_page);

	ptr = ce->lrc_reg_state;
	ptr -= LRC_STATE_OFFSET; /* back to start of context image */
	ptr += context_wa_bb_offset(ce);
	ptr += per_ctx ? PAGE_SIZE : 0;

	return ptr;
}

void lrc_init_state(struct intel_context *ce,
		    struct intel_engine_cs *engine,
		    void *state)
{
	bool inhibit = true;

	set_redzone(state, engine);

	if (ce->default_state) {
#ifdef __linux__
		shmem_read(ce->default_state, 0, state, engine->context_size);
#else
		uao_read(ce->default_state, 0, state, engine->context_size);
#endif
		__set_bit(CONTEXT_VALID_BIT, &ce->flags);
		inhibit = false;
	}

	/* Clear the ppHWSP (inc. per-context counters) */
	memset(state, 0, PAGE_SIZE);

	/* Clear the indirect wa and storage */
	if (ce->wa_bb_page)
		memset(state + context_wa_bb_offset(ce), 0, PAGE_SIZE);

	/*
	 * The second page of the context object contains some registers which
	 * must be set up prior to the first execution.
	 */
	__lrc_init_regs(state + LRC_STATE_OFFSET, ce, engine, inhibit);
}

u32 lrc_indirect_bb(const struct intel_context *ce)
{
	return i915_ggtt_offset(ce->state) + context_wa_bb_offset(ce);
}

static u32 *setup_predicate_disable_wa(const struct intel_context *ce, u32 *cs)
{
	/* If predication is active, this will be noop'ed */
	*cs++ = MI_STORE_DWORD_IMM_GEN4 | MI_USE_GGTT | (4 - 2);
	*cs++ = lrc_indirect_bb(ce) + DG2_PREDICATE_RESULT_WA;
	*cs++ = 0;
	*cs++ = 0; /* No predication */

	/* predicated end, only terminates if SET_PREDICATE_RESULT:0 is clear */
	*cs++ = MI_BATCH_BUFFER_END | BIT(15);
	*cs++ = MI_SET_PREDICATE | MI_SET_PREDICATE_DISABLE;

	/* Instructions are no longer predicated (disabled), we can proceed */
	*cs++ = MI_STORE_DWORD_IMM_GEN4 | MI_USE_GGTT | (4 - 2);
	*cs++ = lrc_indirect_bb(ce) + DG2_PREDICATE_RESULT_WA;
	*cs++ = 0;
	*cs++ = 1; /* enable predication before the next BB */

	*cs++ = MI_BATCH_BUFFER_END;
	GEM_BUG_ON(offset_in_page(cs) > DG2_PREDICATE_RESULT_WA);

	return cs;
}

static struct i915_vma *
__lrc_alloc_state(struct intel_context *ce, struct intel_engine_cs *engine)
{
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	u32 context_size;

	context_size = round_up(engine->context_size, I915_GTT_PAGE_SIZE);

	if (IS_ENABLED(CONFIG_DRM_I915_DEBUG_GEM))
		context_size += I915_GTT_PAGE_SIZE; /* for redzone */

	if (GRAPHICS_VER(engine->i915) >= 12) {
		ce->wa_bb_page = context_size / PAGE_SIZE;
		/* INDIRECT_CTX and PER_CTX_BB need separate pages. */
		context_size += PAGE_SIZE * 2;
	}

	if (intel_context_is_parent(ce) && intel_engine_uses_guc(engine)) {
		ce->parallel.guc.parent_page = context_size / PAGE_SIZE;
		context_size += PARENT_SCRATCH_SIZE;
	}

	obj = i915_gem_object_create_lmem(engine->i915, context_size,
					  I915_BO_ALLOC_PM_VOLATILE);
	if (IS_ERR(obj)) {
		obj = i915_gem_object_create_shmem(engine->i915, context_size);
		if (IS_ERR(obj))
			return ERR_CAST(obj);

		/*
		 * Wa_22016122933: For Media version 13.0, all Media GT shared
		 * memory needs to be mapped as WC on CPU side and UC (PAT
		 * index 2) on GPU side.
		 */
		if (intel_gt_needs_wa_22016122933(engine->gt))
			i915_gem_object_set_cache_coherency(obj, I915_CACHE_NONE);
	}

	vma = i915_vma_instance(obj, &engine->gt->ggtt->vm, NULL);
	if (IS_ERR(vma)) {
		i915_gem_object_put(obj);
		return vma;
	}

	return vma;
}

static struct intel_timeline *
pinned_timeline(struct intel_context *ce, struct intel_engine_cs *engine)
{
	struct intel_timeline *tl = fetch_and_zero(&ce->timeline);

	return intel_timeline_create_from_engine(engine, page_unmask_bits(tl));
}

int lrc_alloc(struct intel_context *ce, struct intel_engine_cs *engine)
{
	struct intel_ring *ring;
	struct i915_vma *vma;
	int err;

	GEM_BUG_ON(ce->state);

	if (!intel_context_has_own_state(ce))
		ce->default_state = engine->default_state;

	vma = __lrc_alloc_state(ce, engine);
	if (IS_ERR(vma))
		return PTR_ERR(vma);

	ring = intel_engine_create_ring(engine, ce->ring_size);
	if (IS_ERR(ring)) {
		err = PTR_ERR(ring);
		goto err_vma;
	}

	if (!page_mask_bits(ce->timeline)) {
		struct intel_timeline *tl;

		/*
		 * Use the static global HWSP for the kernel context, and
		 * a dynamically allocated cacheline for everyone else.
		 */
		if (unlikely(ce->timeline))
			tl = pinned_timeline(ce, engine);
		else
			tl = intel_timeline_create(engine->gt);
		if (IS_ERR(tl)) {
			err = PTR_ERR(tl);
			goto err_ring;
		}

		ce->timeline = tl;
	}

	ce->ring = ring;
	ce->state = vma;

	return 0;

err_ring:
	intel_ring_put(ring);
err_vma:
	i915_vma_put(vma);
	return err;
}

void lrc_reset(struct intel_context *ce)
{
	GEM_BUG_ON(!intel_context_is_pinned(ce));

	intel_ring_reset(ce->ring, ce->ring->emit);

	/* Scrub away the garbage */
	lrc_init_regs(ce, ce->engine, true);
	ce->lrc.lrca = lrc_update_regs(ce, ce->engine, ce->ring->tail);
}

int
lrc_pre_pin(struct intel_context *ce,
	    struct intel_engine_cs *engine,
	    struct i915_gem_ww_ctx *ww,
	    void **vaddr)
{
	GEM_BUG_ON(!ce->state);
	GEM_BUG_ON(!i915_vma_is_pinned(ce->state));

	*vaddr = i915_gem_object_pin_map(ce->state->obj,
					 intel_gt_coherent_map_type(ce->engine->gt,
								    ce->state->obj,
								    false) |
					 I915_MAP_OVERRIDE);

	return PTR_ERR_OR_ZERO(*vaddr);
}

int
lrc_pin(struct intel_context *ce,
	struct intel_engine_cs *engine,
	void *vaddr)
{
	ce->lrc_reg_state = vaddr + LRC_STATE_OFFSET;

	if (!__test_and_set_bit(CONTEXT_INIT_BIT, &ce->flags))
		lrc_init_state(ce, engine, vaddr);

	ce->lrc.lrca = lrc_update_regs(ce, engine, ce->ring->tail);
	return 0;
}

void lrc_unpin(struct intel_context *ce)
{
	if (unlikely(ce->parallel.last_rq)) {
		i915_request_put(ce->parallel.last_rq);
		ce->parallel.last_rq = NULL;
	}
	check_redzone((void *)ce->lrc_reg_state - LRC_STATE_OFFSET,
		      ce->engine);
}

void lrc_post_unpin(struct intel_context *ce)
{
	i915_gem_object_unpin_map(ce->state->obj);
}

void lrc_fini(struct intel_context *ce)
{
	if (!ce->state)
		return;

	intel_ring_put(fetch_and_zero(&ce->ring));
	i915_vma_put(fetch_and_zero(&ce->state));
}

void lrc_destroy(struct kref *kref)
{
	struct intel_context *ce = container_of(kref, typeof(*ce), ref);

	GEM_BUG_ON(!i915_active_is_idle(&ce->active));
	GEM_BUG_ON(intel_context_is_pinned(ce));

	lrc_fini(ce);

	intel_context_fini(ce);
	intel_context_free(ce);
}

static u32 *
gen12_emit_timestamp_wa(const struct intel_context *ce, u32 *cs)
{
	*cs++ = MI_LOAD_REGISTER_MEM_GEN8 |
		MI_SRM_LRM_GLOBAL_GTT |
		MI_LRI_LRM_CS_MMIO;
	*cs++ = i915_mmio_reg_offset(GEN8_RING_CS_GPR(0, 0));
	*cs++ = i915_ggtt_offset(ce->state) + LRC_STATE_OFFSET +
		CTX_TIMESTAMP * sizeof(u32);
	*cs++ = 0;

	*cs++ = MI_LOAD_REGISTER_REG |
		MI_LRR_SOURCE_CS_MMIO |
		MI_LRI_LRM_CS_MMIO;
	*cs++ = i915_mmio_reg_offset(GEN8_RING_CS_GPR(0, 0));
	*cs++ = i915_mmio_reg_offset(RING_CTX_TIMESTAMP(0));

	*cs++ = MI_LOAD_REGISTER_REG |
		MI_LRR_SOURCE_CS_MMIO |
		MI_LRI_LRM_CS_MMIO;
	*cs++ = i915_mmio_reg_offset(GEN8_RING_CS_GPR(0, 0));
	*cs++ = i915_mmio_reg_offset(RING_CTX_TIMESTAMP(0));

	return cs;
}

static u32 *
gen12_emit_restore_scratch(const struct intel_context *ce, u32 *cs)
{
	GEM_BUG_ON(lrc_ring_gpr0(ce->engine) == -1);

	*cs++ = MI_LOAD_REGISTER_MEM_GEN8 |
		MI_SRM_LRM_GLOBAL_GTT |
		MI_LRI_LRM_CS_MMIO;
	*cs++ = i915_mmio_reg_offset(GEN8_RING_CS_GPR(0, 0));
	*cs++ = i915_ggtt_offset(ce->state) + LRC_STATE_OFFSET +
		(lrc_ring_gpr0(ce->engine) + 1) * sizeof(u32);
	*cs++ = 0;

	return cs;
}

static u32 *
gen12_emit_cmd_buf_wa(const struct intel_context *ce, u32 *cs)
{
	GEM_BUG_ON(lrc_ring_cmd_buf_cctl(ce->engine) == -1);

	*cs++ = MI_LOAD_REGISTER_MEM_GEN8 |
		MI_SRM_LRM_GLOBAL_GTT |
		MI_LRI_LRM_CS_MMIO;
	*cs++ = i915_mmio_reg_offset(GEN8_RING_CS_GPR(0, 0));
	*cs++ = i915_ggtt_offset(ce->state) + LRC_STATE_OFFSET +
		(lrc_ring_cmd_buf_cctl(ce->engine) + 1) * sizeof(u32);
	*cs++ = 0;

	*cs++ = MI_LOAD_REGISTER_REG |
		MI_LRR_SOURCE_CS_MMIO |
		MI_LRI_LRM_CS_MMIO;
	*cs++ = i915_mmio_reg_offset(GEN8_RING_CS_GPR(0, 0));
	*cs++ = i915_mmio_reg_offset(RING_CMD_BUF_CCTL(0));

	return cs;
}

/*
 * The bspec's tuning guide asks us to program a vertical watermark value of
 * 0x3FF.  However this register is not saved/restored properly by the
 * hardware, so we're required to apply the desired value via INDIRECT_CTX
 * batch buffer to ensure the value takes effect properly.  All other bits
 * in this register should remain at 0 (the hardware default).
 */
static u32 *
dg2_emit_draw_watermark_setting(u32 *cs)
{
	*cs++ = MI_LOAD_REGISTER_IMM(1);
	*cs++ = i915_mmio_reg_offset(DRAW_WATERMARK);
	*cs++ = REG_FIELD_PREP(VERT_WM_VAL, 0x3FF);

	return cs;
}

static u32 *
gen12_invalidate_state_cache(u32 *cs)
{
	*cs++ = MI_LOAD_REGISTER_IMM(1);
	*cs++ = i915_mmio_reg_offset(GEN12_CS_DEBUG_MODE2);
	*cs++ = _MASKED_BIT_ENABLE(INSTRUCTION_STATE_CACHE_INVALIDATE);
	return cs;
}

static u32 *
gen12_emit_indirect_ctx_rcs(const struct intel_context *ce, u32 *cs)
{
	cs = gen12_emit_timestamp_wa(ce, cs);
	cs = gen12_emit_cmd_buf_wa(ce, cs);
	cs = gen12_emit_restore_scratch(ce, cs);

	/* Wa_16013000631:dg2 */
	if (IS_DG2_G11(ce->engine->i915))
		cs = gen8_emit_pipe_control(cs, PIPE_CONTROL_INSTRUCTION_CACHE_INVALIDATE, 0);

	cs = gen12_emit_aux_table_inv(ce->engine, cs);

	/* Wa_18022495364 */
	if (IS_GFX_GT_IP_RANGE(ce->engine->gt, IP_VER(12, 0), IP_VER(12, 10)))
		cs = gen12_invalidate_state_cache(cs);

	/* Wa_16014892111 */
	if (IS_GFX_GT_IP_STEP(ce->engine->gt, IP_VER(12, 70), STEP_A0, STEP_B0) ||
	    IS_GFX_GT_IP_STEP(ce->engine->gt, IP_VER(12, 71), STEP_A0, STEP_B0) ||
	    IS_DG2(ce->engine->i915))
		cs = dg2_emit_draw_watermark_setting(cs);

	return cs;
}

static u32 *
gen12_emit_indirect_ctx_xcs(const struct intel_context *ce, u32 *cs)
{
	cs = gen12_emit_timestamp_wa(ce, cs);
	cs = gen12_emit_restore_scratch(ce, cs);

	/* Wa_16013000631:dg2 */
	if (IS_DG2_G11(ce->engine->i915))
		if (ce->engine->class == COMPUTE_CLASS)
			cs = gen8_emit_pipe_control(cs,
						    PIPE_CONTROL_INSTRUCTION_CACHE_INVALIDATE,
						    0);

	return gen12_emit_aux_table_inv(ce->engine, cs);
}

static u32 *xehp_emit_fastcolor_blt_wabb(const struct intel_context *ce, u32 *cs)
{
	struct intel_gt *gt = ce->engine->gt;
	int mocs = gt->mocs.uc_index << 1;

	/**
	 * Wa_16018031267 / Wa_16018063123 requires that SW forces the
	 * main copy engine arbitration into round robin mode.  We
	 * additionally need to submit the following WABB blt command
	 * to produce 4 subblits with each subblit generating 0 byte
	 * write requests as WABB:
	 *
	 * XY_FASTCOLOR_BLT
	 *  BG0    -> 5100000E
	 *  BG1    -> 0000003F (Dest pitch)
	 *  BG2    -> 00000000 (X1, Y1) = (0, 0)
	 *  BG3    -> 00040001 (X2, Y2) = (1, 4)
	 *  BG4    -> scratch
	 *  BG5    -> scratch
	 *  BG6-12 -> 00000000
	 *  BG13   -> 20004004 (Surf. Width= 2,Surf. Height = 5 )
	 *  BG14   -> 00000010 (Qpitch = 4)
	 *  BG15   -> 00000000
	 */
	*cs++ = XY_FAST_COLOR_BLT_CMD | (16 - 2);
	*cs++ = FIELD_PREP(XY_FAST_COLOR_BLT_MOCS_MASK, mocs) | 0x3f;
	*cs++ = 0;
	*cs++ = 4 << 16 | 1;
	*cs++ = lower_32_bits(i915_vma_offset(ce->vm->rsvd.vma));
	*cs++ = upper_32_bits(i915_vma_offset(ce->vm->rsvd.vma));
	*cs++ = 0;
	*cs++ = 0;
	*cs++ = 0;
	*cs++ = 0;
	*cs++ = 0;
	*cs++ = 0;
	*cs++ = 0;
	*cs++ = 0x20004004;
	*cs++ = 0x10;
	*cs++ = 0;

	return cs;
}

static u32 *
xehp_emit_per_ctx_bb(const struct intel_context *ce, u32 *cs)
{
	/* Wa_16018031267, Wa_16018063123 */
	if (NEEDS_FASTCOLOR_BLT_WABB(ce->engine))
		cs = xehp_emit_fastcolor_blt_wabb(ce, cs);

	return cs;
}

static void
setup_per_ctx_bb(const struct intel_context *ce,
		 const struct intel_engine_cs *engine,
		 u32 *(*emit)(const struct intel_context *, u32 *))
{
	/* Place PER_CTX_BB on next page after INDIRECT_CTX */
	u32 * const start = context_wabb(ce, true);
	u32 *cs;

	cs = emit(ce, start);

	/* PER_CTX_BB must manually terminate */
	*cs++ = MI_BATCH_BUFFER_END;

	GEM_BUG_ON(cs - start > I915_GTT_PAGE_SIZE / sizeof(*cs));
	lrc_setup_bb_per_ctx(ce->lrc_reg_state, engine,
			     lrc_indirect_bb(ce) + PAGE_SIZE);
}

static void
setup_indirect_ctx_bb(const struct intel_context *ce,
		      const struct intel_engine_cs *engine,
		      u32 *(*emit)(const struct intel_context *, u32 *))
{
	u32 * const start = context_wabb(ce, false);
	u32 *cs;

	cs = emit(ce, start);
	GEM_BUG_ON(cs - start > I915_GTT_PAGE_SIZE / sizeof(*cs));
	while ((unsigned long)cs % CACHELINE_BYTES)
		*cs++ = MI_NOOP;

	GEM_BUG_ON(cs - start > DG2_PREDICATE_RESULT_BB / sizeof(*start));
	setup_predicate_disable_wa(ce, start + DG2_PREDICATE_RESULT_BB / sizeof(*start));

	lrc_setup_indirect_ctx(ce->lrc_reg_state, engine,
			       lrc_indirect_bb(ce),
			       (cs - start) * sizeof(*cs));
}

/*
 * The context descriptor encodes various attributes of a context,
 * including its GTT address and some flags. Because it's fairly
 * expensive to calculate, we'll just do it once and cache the result,
 * which remains valid until the context is unpinned.
 *
 * This is what a descriptor looks like, from LSB to MSB::
 *
 *      bits  0-11:    flags, GEN8_CTX_* (cached in ctx->desc_template)
 *      bits 12-31:    LRCA, GTT address of (the HWSP of) this context
 *      bits 32-52:    ctx ID, a globally unique tag (highest bit used by GuC)
 *      bits 53-54:    mbz, reserved for use by hardware
 *      bits 55-63:    group ID, currently unused and set to 0
 *
 * Starting from Gen11, the upper dword of the descriptor has a new format:
 *
 *      bits 32-36:    reserved
 *      bits 37-47:    SW context ID
 *      bits 48:53:    engine instance
 *      bit 54:        mbz, reserved for use by hardware
 *      bits 55-60:    SW counter
 *      bits 61-63:    engine class
 *
 * On Xe_HP, the upper dword of the descriptor has a new format:
 *
 *      bits 32-37:    virtual function number
 *      bit 38:        mbz, reserved for use by hardware
 *      bits 39-54:    SW context ID
 *      bits 55-57:    reserved
 *      bits 58-63:    SW counter
 *
 * engine info, SW context ID and SW counter need to form a unique number
 * (Context ID) per lrc.
 */
static u32 lrc_descriptor(const struct intel_context *ce)
{
	u32 desc;

	desc = INTEL_LEGACY_32B_CONTEXT;
	if (i915_vm_is_4lvl(ce->vm))
		desc = INTEL_LEGACY_64B_CONTEXT;
	desc <<= GEN8_CTX_ADDRESSING_MODE_SHIFT;

	desc |= GEN8_CTX_VALID | GEN8_CTX_PRIVILEGE;
	if (GRAPHICS_VER(ce->vm->i915) == 8)
		desc |= GEN8_CTX_L3LLC_COHERENT;

	return i915_ggtt_offset(ce->state) | desc;
}

u32 lrc_update_regs(const struct intel_context *ce,
		    const struct intel_engine_cs *engine,
		    u32 head)
{
	struct intel_ring *ring = ce->ring;
	u32 *regs = ce->lrc_reg_state;

	GEM_BUG_ON(!intel_ring_offset_valid(ring, head));
	GEM_BUG_ON(!intel_ring_offset_valid(ring, ring->tail));

	regs[CTX_RING_START] = i915_ggtt_offset(ring->vma);
	regs[CTX_RING_HEAD] = head;
	regs[CTX_RING_TAIL] = ring->tail;
	regs[CTX_RING_CTL] = RING_CTL_SIZE(ring->size) | RING_VALID;

	/* RPCS */
	if (engine->class == RENDER_CLASS) {
		regs[CTX_R_PWR_CLK_STATE] =
			intel_sseu_make_rpcs(engine->gt, &ce->sseu);

		i915_oa_init_reg_state(ce, engine);
	}

	if (ce->wa_bb_page) {
		u32 *(*fn)(const struct intel_context *ce, u32 *cs);

		fn = gen12_emit_indirect_ctx_xcs;
		if (ce->engine->class == RENDER_CLASS)
			fn = gen12_emit_indirect_ctx_rcs;

		/* Mutually exclusive wrt to global indirect bb */
		GEM_BUG_ON(engine->wa_ctx.indirect_ctx.size);
		setup_indirect_ctx_bb(ce, engine, fn);
		setup_per_ctx_bb(ce, engine, xehp_emit_per_ctx_bb);
	}

	return lrc_descriptor(ce) | CTX_DESC_FORCE_RESTORE;
}

void lrc_update_offsets(struct intel_context *ce,
			struct intel_engine_cs *engine)
{
	set_offsets(ce->lrc_reg_state, reg_offsets(engine), engine, false);
}

void lrc_check_regs(const struct intel_context *ce,
		    const struct intel_engine_cs *engine,
		    const char *when)
{
	const struct intel_ring *ring = ce->ring;
	u32 *regs = ce->lrc_reg_state;
	bool valid = true;
	int x;

	if (regs[CTX_RING_START] != i915_ggtt_offset(ring->vma)) {
		pr_err("%s: context submitted with incorrect RING_START [%08x], expected %08x\n",
		       engine->name,
		       regs[CTX_RING_START],
		       i915_ggtt_offset(ring->vma));
		regs[CTX_RING_START] = i915_ggtt_offset(ring->vma);
		valid = false;
	}

	if ((regs[CTX_RING_CTL] & ~(RING_WAIT | RING_WAIT_SEMAPHORE)) !=
	    (RING_CTL_SIZE(ring->size) | RING_VALID)) {
		pr_err("%s: context submitted with incorrect RING_CTL [%08x], expected %08x\n",
		       engine->name,
		       regs[CTX_RING_CTL],
		       (u32)(RING_CTL_SIZE(ring->size) | RING_VALID));
		regs[CTX_RING_CTL] = RING_CTL_SIZE(ring->size) | RING_VALID;
		valid = false;
	}

	x = lrc_ring_mi_mode(engine);
	if (x != -1 && regs[x + 1] & (regs[x + 1] >> 16) & STOP_RING) {
		pr_err("%s: context submitted with STOP_RING [%08x] in RING_MI_MODE\n",
		       engine->name, regs[x + 1]);
		regs[x + 1] &= ~STOP_RING;
		regs[x + 1] |= STOP_RING << 16;
		valid = false;
	}

	WARN_ONCE(!valid, "Invalid lrc state found %s submission\n", when);
}

/*
 * In this WA we need to set GEN8_L3SQCREG4[21:21] and reset it after
 * PIPE_CONTROL instruction. This is required for the flush to happen correctly
 * but there is a slight complication as this is applied in WA batch where the
 * values are only initialized once so we cannot take register value at the
 * beginning and reuse it further; hence we save its value to memory, upload a
 * constant value with bit21 set and then we restore it back with the saved value.
 * To simplify the WA, a constant value is formed by using the default value
 * of this register. This shouldn't be a problem because we are only modifying
 * it for a short period and this batch in non-premptible. We can ofcourse
 * use additional instructions that read the actual value of the register
 * at that time and set our bit of interest but it makes the WA complicated.
 *
 * This WA is also required for Gen9 so extracting as a function avoids
 * code duplication.
 */
static u32 *
gen8_emit_flush_coherentl3_wa(struct intel_engine_cs *engine, u32 *batch)
{
	/* NB no one else is allowed to scribble over scratch + 256! */
	*batch++ = MI_STORE_REGISTER_MEM_GEN8 | MI_SRM_LRM_GLOBAL_GTT;
	*batch++ = i915_mmio_reg_offset(GEN8_L3SQCREG4);
	*batch++ = intel_gt_scratch_offset(engine->gt,
					   INTEL_GT_SCRATCH_FIELD_COHERENTL3_WA);
	*batch++ = 0;

	*batch++ = MI_LOAD_REGISTER_IMM(1);
	*batch++ = i915_mmio_reg_offset(GEN8_L3SQCREG4);
	*batch++ = 0x40400000 | GEN8_LQSC_FLUSH_COHERENT_LINES;

	batch = gen8_emit_pipe_control(batch,
				       PIPE_CONTROL_CS_STALL |
				       PIPE_CONTROL_DC_FLUSH_ENABLE,
				       0);

	*batch++ = MI_LOAD_REGISTER_MEM_GEN8 | MI_SRM_LRM_GLOBAL_GTT;
	*batch++ = i915_mmio_reg_offset(GEN8_L3SQCREG4);
	*batch++ = intel_gt_scratch_offset(engine->gt,
					   INTEL_GT_SCRATCH_FIELD_COHERENTL3_WA);
	*batch++ = 0;

	return batch;
}

/*
 * Typically we only have one indirect_ctx and per_ctx batch buffer which are
 * initialized at the beginning and shared across all contexts but this field
 * helps us to have multiple batches at different offsets and select them based
 * on a criteria. At the moment this batch always start at the beginning of the page
 * and at this point we don't have multiple wa_ctx batch buffers.
 *
 * The number of WA applied are not known at the beginning; we use this field
 * to return the no of DWORDS written.
 *
 * It is to be noted that this batch does not contain MI_BATCH_BUFFER_END
 * so it adds NOOPs as padding to make it cacheline aligned.
 * MI_BATCH_BUFFER_END will be added to perctx batch and both of them together
 * makes a complete batch buffer.
 */
static u32 *gen8_init_indirectctx_bb(struct intel_engine_cs *engine, u32 *batch)
{
	/* WaDisableCtxRestoreArbitration:bdw,chv */
	*batch++ = MI_ARB_ON_OFF | MI_ARB_DISABLE;

	/* WaFlushCoherentL3CacheLinesAtContextSwitch:bdw */
	if (IS_BROADWELL(engine->i915))
		batch = gen8_emit_flush_coherentl3_wa(engine, batch);

	/* WaClearSlmSpaceAtContextSwitch:bdw,chv */
	/* Actual scratch location is at 128 bytes offset */
	batch = gen8_emit_pipe_control(batch,
				       PIPE_CONTROL_FLUSH_L3 |
				       PIPE_CONTROL_STORE_DATA_INDEX |
				       PIPE_CONTROL_CS_STALL |
				       PIPE_CONTROL_QW_WRITE,
				       LRC_PPHWSP_SCRATCH_ADDR);

	*batch++ = MI_ARB_ON_OFF | MI_ARB_ENABLE;

	/* Pad to end of cacheline */
	while ((unsigned long)batch % CACHELINE_BYTES)
		*batch++ = MI_NOOP;

	/*
	 * MI_BATCH_BUFFER_END is not required in Indirect ctx BB because
	 * execution depends on the length specified in terms of cache lines
	 * in the register CTX_RCS_INDIRECT_CTX
	 */

	return batch;
}

struct lri {
	i915_reg_t reg;
	u32 value;
};

static u32 *emit_lri(u32 *batch, const struct lri *lri, unsigned int count)
{
	GEM_BUG_ON(!count || count > 63);

	*batch++ = MI_LOAD_REGISTER_IMM(count);
	do {
		*batch++ = i915_mmio_reg_offset(lri->reg);
		*batch++ = lri->value;
	} while (lri++, --count);
	*batch++ = MI_NOOP;

	return batch;
}

static u32 *gen9_init_indirectctx_bb(struct intel_engine_cs *engine, u32 *batch)
{
	static const struct lri lri[] = {
		/* WaDisableGatherAtSetShaderCommonSlice:skl,bxt,kbl,glk */
		{
			COMMON_SLICE_CHICKEN2,
			__MASKED_FIELD(GEN9_DISABLE_GATHER_AT_SET_SHADER_COMMON_SLICE,
				       0),
		},

		/* BSpec: 11391 */
		{
			FF_SLICE_CHICKEN,
			__MASKED_FIELD(FF_SLICE_CHICKEN_CL_PROVOKING_VERTEX_FIX,
				       FF_SLICE_CHICKEN_CL_PROVOKING_VERTEX_FIX),
		},

		/* BSpec: 11299 */
		{
			_3D_CHICKEN3,
			__MASKED_FIELD(_3D_CHICKEN_SF_PROVOKING_VERTEX_FIX,
				       _3D_CHICKEN_SF_PROVOKING_VERTEX_FIX),
		}
	};

	*batch++ = MI_ARB_ON_OFF | MI_ARB_DISABLE;

	/* WaFlushCoherentL3CacheLinesAtContextSwitch:skl,bxt,glk */
	batch = gen8_emit_flush_coherentl3_wa(engine, batch);

	/* WaClearSlmSpaceAtContextSwitch:skl,bxt,kbl,glk,cfl */
	batch = gen8_emit_pipe_control(batch,
				       PIPE_CONTROL_FLUSH_L3 |
				       PIPE_CONTROL_STORE_DATA_INDEX |
				       PIPE_CONTROL_CS_STALL |
				       PIPE_CONTROL_QW_WRITE,
				       LRC_PPHWSP_SCRATCH_ADDR);

	batch = emit_lri(batch, lri, ARRAY_SIZE(lri));

	/* WaMediaPoolStateCmdInWABB:bxt,glk */
	if (HAS_POOLED_EU(engine->i915)) {
		/*
		 * EU pool configuration is setup along with golden context
		 * during context initialization. This value depends on
		 * device type (2x6 or 3x6) and needs to be updated based
		 * on which subslice is disabled especially for 2x6
		 * devices, however it is safe to load default
		 * configuration of 3x6 device instead of masking off
		 * corresponding bits because HW ignores bits of a disabled
		 * subslice and drops down to appropriate config. Please
		 * see render_state_setup() in i915_gem_render_state.c for
		 * possible configurations, to avoid duplication they are
		 * not shown here again.
		 */
		*batch++ = GEN9_MEDIA_POOL_STATE;
		*batch++ = GEN9_MEDIA_POOL_ENABLE;
		*batch++ = 0x00777000;
		*batch++ = 0;
		*batch++ = 0;
		*batch++ = 0;
	}

	*batch++ = MI_ARB_ON_OFF | MI_ARB_ENABLE;

	/* Pad to end of cacheline */
	while ((unsigned long)batch % CACHELINE_BYTES)
		*batch++ = MI_NOOP;

	return batch;
}

#define CTX_WA_BB_SIZE (PAGE_SIZE)

static int lrc_create_wa_ctx(struct intel_engine_cs *engine)
{
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	int err;

	obj = i915_gem_object_create_shmem(engine->i915, CTX_WA_BB_SIZE);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	vma = i915_vma_instance(obj, &engine->gt->ggtt->vm, NULL);
	if (IS_ERR(vma)) {
		err = PTR_ERR(vma);
		goto err;
	}

	engine->wa_ctx.vma = vma;
	return 0;

err:
	i915_gem_object_put(obj);
	return err;
}

void lrc_fini_wa_ctx(struct intel_engine_cs *engine)
{
	i915_vma_unpin_and_release(&engine->wa_ctx.vma, 0);
}

typedef u32 *(*wa_bb_func_t)(struct intel_engine_cs *engine, u32 *batch);

void lrc_init_wa_ctx(struct intel_engine_cs *engine)
{
	struct i915_ctx_workarounds *wa_ctx = &engine->wa_ctx;
	struct i915_wa_ctx_bb *wa_bb[] = {
		&wa_ctx->indirect_ctx, &wa_ctx->per_ctx
	};
	wa_bb_func_t wa_bb_fn[ARRAY_SIZE(wa_bb)];
	struct i915_gem_ww_ctx ww;
	void *batch, *batch_ptr;
	unsigned int i;
	int err;

	if (GRAPHICS_VER(engine->i915) >= 11 ||
	    !(engine->flags & I915_ENGINE_HAS_RCS_REG_STATE))
		return;

	if (GRAPHICS_VER(engine->i915) == 9) {
		wa_bb_fn[0] = gen9_init_indirectctx_bb;
		wa_bb_fn[1] = NULL;
	} else if (GRAPHICS_VER(engine->i915) == 8) {
		wa_bb_fn[0] = gen8_init_indirectctx_bb;
		wa_bb_fn[1] = NULL;
	}

	err = lrc_create_wa_ctx(engine);
	if (err) {
		/*
		 * We continue even if we fail to initialize WA batch
		 * because we only expect rare glitches but nothing
		 * critical to prevent us from using GPU
		 */
		drm_err(&engine->i915->drm,
			"Ignoring context switch w/a allocation error:%d\n",
			err);
		return;
	}

	if (!engine->wa_ctx.vma)
		return;

	i915_gem_ww_ctx_init(&ww, true);
retry:
	err = i915_gem_object_lock(wa_ctx->vma->obj, &ww);
	if (!err)
		err = i915_ggtt_pin(wa_ctx->vma, &ww, 0, PIN_HIGH);
	if (err)
		goto err;

	batch = i915_gem_object_pin_map(wa_ctx->vma->obj, I915_MAP_WB);
	if (IS_ERR(batch)) {
		err = PTR_ERR(batch);
		goto err_unpin;
	}

	/*
	 * Emit the two workaround batch buffers, recording the offset from the
	 * start of the workaround batch buffer object for each and their
	 * respective sizes.
	 */
	batch_ptr = batch;
	for (i = 0; i < ARRAY_SIZE(wa_bb_fn); i++) {
		wa_bb[i]->offset = batch_ptr - batch;
		if (GEM_DEBUG_WARN_ON(!IS_ALIGNED(wa_bb[i]->offset,
						  CACHELINE_BYTES))) {
			err = -EINVAL;
			break;
		}
		if (wa_bb_fn[i])
			batch_ptr = wa_bb_fn[i](engine, batch_ptr);
		wa_bb[i]->size = batch_ptr - (batch + wa_bb[i]->offset);
	}
	GEM_BUG_ON(batch_ptr - batch > CTX_WA_BB_SIZE);

	__i915_gem_object_flush_map(wa_ctx->vma->obj, 0, batch_ptr - batch);
	__i915_gem_object_release_map(wa_ctx->vma->obj);

	/* Verify that we can handle failure to setup the wa_ctx */
	if (!err)
		err = i915_inject_probe_error(engine->i915, -ENODEV);

err_unpin:
	if (err)
		i915_vma_unpin(wa_ctx->vma);
err:
	if (err == -EDEADLK) {
		err = i915_gem_ww_ctx_backoff(&ww);
		if (!err)
			goto retry;
	}
	i915_gem_ww_ctx_fini(&ww);

	if (err) {
		i915_vma_put(engine->wa_ctx.vma);

		/* Clear all flags to prevent further use */
		memset(wa_ctx, 0, sizeof(*wa_ctx));
	}
}

static void st_runtime_underflow(struct intel_context_stats *stats, s32 dt)
{
#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
	stats->runtime.num_underflow++;
	stats->runtime.max_underflow =
		max_t(u32, stats->runtime.max_underflow, -dt);
#endif
}

static u32 lrc_get_runtime(const struct intel_context *ce)
{
	/*
	 * We can use either ppHWSP[16] which is recorded before the context
	 * switch (and so excludes the cost of context switches) or use the
	 * value from the context image itself, which is saved/restored earlier
	 * and so includes the cost of the save.
	 */
	return READ_ONCE(ce->lrc_reg_state[CTX_TIMESTAMP]);
}

void lrc_update_runtime(struct intel_context *ce)
{
	struct intel_context_stats *stats = &ce->stats;
	u32 old;
	s32 dt;

	old = stats->runtime.last;
	stats->runtime.last = lrc_get_runtime(ce);
	dt = stats->runtime.last - old;
	if (!dt)
		return;

	if (unlikely(dt < 0)) {
		CE_TRACE(ce, "runtime underflow: last=%u, new=%u, delta=%d\n",
			 old, stats->runtime.last, dt);
		st_runtime_underflow(stats, dt);
		return;
	}

	ewma_runtime_add(&stats->runtime.avg, dt);
	stats->runtime.total += dt;
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftest_lrc.c"
#endif
