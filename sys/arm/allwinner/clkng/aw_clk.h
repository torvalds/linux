/*-
 * Copyright (c) 2017 Emmanuel Vadot <manu@freebsd.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	__AW_CLK_H__
#define __AW_CLK_H__

/*
  Allwinner clocks formula :

PLLs:

(24MHz*N*K)/(M*P)
(24MHz*N)/(M*P)
(24MHz*N*2)/M
(24MHz*N)/M
(24MHz*N*K)/M
(24MHz*N*K/2)
(24MHz*N)/M
(24MHz*N*K/2)
(24MHz*N)/M

Periph clocks:

Clock Source/Divider N/Divider M
Clock Source/Divider N/Divider M/2
Clock Source*N/(Divider M+1)/(Divider P+1)

 */

struct aw_clk_init {
	const char	*name;
	const char	*parent_name;
	uint64_t	default_freq;
	bool		enable;
};

#define	AW_CLK_HAS_GATE		0x0001
#define	AW_CLK_HAS_LOCK		0x0002
#define	AW_CLK_HAS_MUX		0x0004
#define	AW_CLK_REPARENT		0x0008
#define	AW_CLK_SCALE_CHANGE	0x0010
#define	AW_CLK_HAS_FRAC		0x0020
#define	AW_CLK_HAS_UPDATE	0x0040
#define	AW_CLK_HAS_PREDIV	0x0080

#define	AW_CLK_FACTOR_POWER_OF_TWO	0x0001
#define	AW_CLK_FACTOR_ZERO_BASED	0x0002
#define	AW_CLK_FACTOR_HAS_COND		0x0004
#define	AW_CLK_FACTOR_FIXED		0x0008
#define	AW_CLK_FACTOR_ZERO_IS_ONE	0x0010

struct aw_clk_factor {
	uint32_t	shift;		/* Shift bits for the factor */
	uint32_t	mask;		/* Mask to get the factor, will be override by the clk methods */
	uint32_t	width;		/* Number of bits for the factor */
	uint32_t	value;		/* Fixed value, depends on AW_CLK_FACTOR_FIXED */

	uint32_t	cond_shift;
	uint32_t	cond_mask;
	uint32_t	cond_width;
	uint32_t	cond_value;

	uint32_t	flags;		/* Flags */
};

struct aw_clk_frac {
	uint64_t	freq0;
	uint64_t	freq1;
	uint32_t	mode_sel;
	uint32_t	freq_sel;
};

static inline uint32_t
aw_clk_get_factor(uint32_t val, struct aw_clk_factor *factor)
{
	uint32_t factor_val;
	uint32_t cond;

	if (factor->flags & AW_CLK_FACTOR_HAS_COND) {
		cond = (val & factor->cond_mask) >> factor->cond_shift;
		if (cond != factor->cond_value)
			return (1);
	}

	if (factor->flags & AW_CLK_FACTOR_FIXED)
		return (factor->value);

	factor_val = (val & factor->mask) >> factor->shift;
	if (factor_val == 0 && (factor->flags & AW_CLK_FACTOR_ZERO_IS_ONE))
		factor_val = 1;

	if (factor->flags & AW_CLK_FACTOR_POWER_OF_TWO)
		factor_val = 1 << factor_val;
	else if (!(factor->flags & AW_CLK_FACTOR_ZERO_BASED))
		factor_val += 1;

	return (factor_val);
}

static inline uint32_t
aw_clk_factor_get_max(struct aw_clk_factor *factor)
{
	uint32_t max;

	if (factor->flags & AW_CLK_FACTOR_FIXED)
		max = factor->value;
	else if (factor->flags & AW_CLK_FACTOR_POWER_OF_TWO)
		max = 1 << ((1 << factor->width) - 1);
	else {
		max = (1 << factor->width);
	}

	return (max);
}

static inline uint32_t
aw_clk_factor_get_min(struct aw_clk_factor *factor)
{
	uint32_t min;

	if (factor->flags & AW_CLK_FACTOR_FIXED)
		min = factor->value;
	else if (factor->flags & AW_CLK_FACTOR_ZERO_BASED)
		min = 0;
	else
		min = 1;

	return (min);
}

static inline uint32_t
aw_clk_factor_get_value(struct aw_clk_factor *factor, uint32_t raw)
{
	uint32_t val;

	if (factor->flags & AW_CLK_FACTOR_FIXED)
		return (factor->value);

	if (factor->flags & AW_CLK_FACTOR_ZERO_BASED)
		val = raw;
	else if (factor->flags & AW_CLK_FACTOR_POWER_OF_TWO) {
		for (val = 0; raw != 1; val++)
			raw >>= 1;
	} else
		val = raw - 1;

	return (val);
}

#define	CCU_RESET(idx, o, s)	\
	[idx] = {		\
		.offset = o,	\
		.shift = s,	\
	},

#define	CCU_GATE(idx, clkname, pname, o, s)	\
	[idx] = {				\
		.name = clkname,		\
		.parent_name = pname,		\
		.offset = o,			\
		.shift = s,			\
	},

#define NKMP_CLK(_clkname, _id, _name, _pnames,		\
  _offset,						\
  _n_shift, _n_width, _n_value, _n_flags,		\
  _k_shift, _k_width, _k_value, _k_flags,		\
  _m_shift, _m_width, _m_value, _m_flags,		\
  _p_shift, _p_width, _p_value, _p_flags,		\
  _gate,						\
  _lock, _lock_retries,					\
  _flags)						\
	static struct aw_clk_nkmp_def _clkname = {	\
		.clkdef = {				\
			.id = _id,			\
			.name = _name,			\
			.parent_names = _pnames,	\
			.parent_cnt = nitems(_pnames),	\
		},					\
		.offset = _offset,			\
		.n.shift = _n_shift,			\
		.n.width = _n_width,			\
		.n.value = _n_value,			\
		.n.flags = _n_flags,			\
		.k.shift = _k_shift,			\
		.k.width = _k_width,			\
		.k.value = _k_value,			\
		.k.flags = _k_flags,			\
		.m.shift = _m_shift,			\
		.m.width = _m_width,			\
		.m.value = _m_value,			\
		.m.flags = _m_flags,			\
		.p.shift = _p_shift,			\
		.p.width = _p_width,			\
		.p.value = _p_value,			\
		.p.flags = _p_flags,			\
		.gate_shift = _gate,			\
		.lock_shift = _lock,			\
		.lock_retries = _lock_retries,		\
		.flags = _flags,			\
	}

#define NKMP_CLK_WITH_MUX(_clkname,			\
  _id, _name, _pnames,					\
  _offset,						\
  _n_shift, _n_width, _n_value, _n_flags,		\
  _k_shift, _k_width, _k_value, _k_flags,		\
  _m_shift, _m_width, _m_value, _m_flags,		\
  _p_shift, _p_width, _p_value, _p_flags,		\
  _mux_shift, _mux_width, _gate,			\
  _lock, _lock_retries,					\
  _flags)						\
	static struct aw_clk_nkmp_def _clkname = {	\
		.clkdef = {				\
			.id = _id,			\
			.name = _name,			\
			.parent_names = _pnames,	\
			.parent_cnt = nitems(_pnames),	\
		},					\
		.offset = _offset,			\
		.n.shift = _n_shift,			\
		.n.width = _n_width,			\
		.n.value = _n_value,			\
		.n.flags = _n_flags,			\
		.k.shift = _k_shift,			\
		.k.width = _k_width,			\
		.k.value = _k_value,			\
		.k.flags = _k_flags,			\
		.m.shift = _m_shift,			\
		.m.width = _m_width,			\
		.m.value = _m_value,			\
		.m.flags = _m_flags,			\
		.p.shift = _p_shift,			\
		.p.width = _p_width,			\
		.p.value = _p_value,			\
		.p.flags = _p_flags,			\
		.mux_shift = _mux_shift,		\
		.mux_width = _mux_width,		\
		.gate_shift = _gate,			\
		.lock_shift = _lock,			\
		.lock_retries = _lock_retries,		\
		.flags = _flags,			\
	}

#define NKMP_CLK_WITH_UPDATE(_clkname,			\
  _id, _name, _pnames,					\
  _offset,						\
  _n_shift, _n_width, _n_value, _n_flags,		\
  _k_shift, _k_width, _k_value, _k_flags,		\
  _m_shift, _m_width, _m_value, _m_flags,		\
  _p_shift, _p_width, _p_value, _p_flags,		\
  _gate,						\
  _lock, _lock_retries,					\
  _update,						\
  _flags)						\
	static struct aw_clk_nkmp_def _clkname = {	\
		.clkdef = {				\
			.id = _id,			\
			.name = _name,			\
			.parent_names = _pnames,	\
			.parent_cnt = nitems(_pnames),	\
		},					\
		.offset = _offset,			\
		.n.shift = _n_shift,			\
		.n.width = _n_width,			\
		.n.value = _n_value,			\
		.n.flags = _n_flags,			\
		.k.shift = _k_shift,			\
		.k.width = _k_width,			\
		.k.value = _k_value,			\
		.k.flags = _k_flags,			\
		.m.shift = _m_shift,			\
		.m.width = _m_width,			\
		.m.value = _m_value,			\
		.m.flags = _m_flags,			\
		.p.shift = _p_shift,			\
		.p.width = _p_width,			\
		.p.value = _p_value,			\
		.p.flags = _p_flags,			\
		.gate_shift = _gate,			\
		.lock_shift = _lock,			\
		.lock_retries = _lock_retries,		\
		.update_shift = _update,		\
		.flags = _flags | AW_CLK_HAS_UPDATE,	\
	}

#define NM_CLK(_clkname, _id, _name, _pnames,		\
     _offset,						\
     _nshift, _nwidth, _nvalue, _nflags,		\
     _mshift, _mwidth, _mvalue, _mflags,		\
    _mux_shift, _mux_width,				\
    _gate_shift,					\
    _flags)						\
	static struct aw_clk_nm_def _clkname = 	{	\
		.clkdef = {				\
			.id = _id,			\
			.name = _name,			\
			.parent_names = _pnames,	\
			.parent_cnt = nitems(_pnames),	\
		},					\
		.offset = _offset,			\
		.n.shift = _nshift,			\
		.n.width = _nwidth,			\
		.n.value = _nvalue,			\
		.n.flags = _nflags,			\
		.mux_shift = _mux_shift,		\
		.m.shift = _mshift,			\
		.m.width = _mwidth,			\
		.m.value = _mvalue,			\
		.m.flags = _mflags,			\
		.mux_width = _mux_width,		\
		.gate_shift = _gate_shift,		\
		.flags = _flags,			\
	}

#define NM_CLK_WITH_FRAC(_clkname, _id, _name, _pnames,	\
     _offset,						\
     _nshift, _nwidth, _nvalue, _nflags,		\
     _mshift, _mwidth, _mvalue, _mflags,		\
     _gate_shift, _lock_shift,_lock_retries,		\
    _flags, _freq0, _freq1, _mode_sel, _freq_sel)	\
	static struct aw_clk_nm_def _clkname =	{	\
		.clkdef = {				\
			.id = _id,			\
			.name = _name,			\
			.parent_names = _pnames,	\
			.parent_cnt = nitems(_pnames),	\
		},					\
		.offset = _offset,			\
		.n.shift = _nshift,			\
		.n.width = _nwidth,			\
		.n.value = _nvalue,			\
		.n.flags = _nflags,			\
		.m.shift = _mshift,			\
		.m.width = _mwidth,			\
		.m.value = _mvalue,			\
		.m.flags = _mflags,			\
		.gate_shift = _gate_shift,		\
		.lock_shift = _lock_shift,		\
		.lock_retries = _lock_retries,		\
		.flags = _flags | AW_CLK_HAS_FRAC,	\
		.frac.freq0 = _freq0,			\
		.frac.freq1 = _freq1,			\
		.frac.mode_sel = _mode_sel,		\
		.frac.freq_sel = _freq_sel,		\
	}

#define PREDIV_CLK(_clkname, _id, _name, _pnames,	\
  _offset,	\
  _mux_shift, _mux_width,	\
  _div_shift, _div_width, _div_value, _div_flags,	\
  _prediv_shift, _prediv_width, _prediv_value, _prediv_flags,	\
  _prediv_cond_shift, _prediv_cond_width, _prediv_cond_value)	\
	static struct aw_clk_prediv_mux_def _clkname = {	\
		.clkdef = {					\
			.id = _id,				\
			.name = _name,				\
			.parent_names = _pnames,		\
			.parent_cnt = nitems(_pnames),		\
		},						\
		.offset = _offset,				\
		.mux_shift = _mux_shift,			\
		.mux_width = _mux_width,			\
		.div.shift = _div_shift,			\
		.div.width = _div_width,			\
		.div.value = _div_value,			\
		.div.flags = _div_flags,			\
		.prediv.shift = _prediv_shift,			\
		.prediv.width = _prediv_width,			\
		.prediv.value = _prediv_value,			\
		.prediv.flags = _prediv_flags,			\
		.prediv.cond_shift = _prediv_cond_shift,	\
		.prediv.cond_width = _prediv_cond_width,	\
		.prediv.cond_value = _prediv_cond_value,	\
	}

#define PREDIV_CLK_WITH_MASK(_clkname, _id, _name, _pnames,	\
  _offset,							\
  _mux_shift, _mux_width,					\
  _div_shift, _div_width, _div_value, _div_flags,		\
  _prediv_shift, _prediv_width, _prediv_value, _prediv_flags,	\
  _prediv_cond_mask, _prediv_cond_value)			\
	static struct aw_clk_prediv_mux_def _clkname = {	\
		.clkdef = {					\
			.id = _id,				\
			.name = _name,				\
			.parent_names = _pnames,		\
			.parent_cnt = nitems(_pnames),		\
		},						\
		.offset = _offset,				\
		.mux_shift = _mux_shift,			\
		.mux_width = _mux_width,			\
		.div.shift = _div_shift,			\
		.div.width = _div_width,			\
		.div.value = _div_value,			\
		.div.flags = _div_flags,			\
		.prediv.shift = _prediv_shift,			\
		.prediv.width = _prediv_width,			\
		.prediv.value = _prediv_value,			\
		.prediv.flags = _prediv_flags,			\
		.prediv.cond_shift = 0,				\
		.prediv.cond_width = 0,				\
		.prediv.cond_mask = _prediv_cond_mask,		\
		.prediv.cond_value = _prediv_cond_value,	\
	}

#define MUX_CLK(_clkname, _id, _name, _pnames,		\
  _offset,  _shift,  _width)				\
	static struct clk_mux_def _clkname = {	\
		.clkdef = {				\
			.id = _id,			\
			.name = _name,			\
			.parent_names = _pnames,	\
			.parent_cnt = nitems(_pnames)	\
		},					\
		.offset = _offset,			\
		.shift = _shift,			\
		.width = _width,			\
	}

#define DIV_CLK(_clkname, _id, _name, _pnames,		\
  _offset,						\
  _i_shift, _i_width,					\
  _div_flags, _div_table)				\
	static struct clk_div_def _clkname = {		\
		.clkdef = {				\
			.id = _id,			\
			.name = _name,			\
			.parent_names = _pnames,	\
			.parent_cnt = nitems(_pnames)	\
		},					\
		.offset = _offset,			\
		.i_shift = _i_shift,			\
		.i_width = _i_width,			\
		.div_flags = _div_flags,		\
		.div_table = _div_table,		\
	}

#define FIXED_CLK(_clkname, _id, _name, _pnames,	\
  _freq, _mult, _div, _flags)				\
	static struct clk_fixed_def _clkname = {	\
		.clkdef = {				\
			.id = _id,			\
			.name = _name,			\
			.parent_names = _pnames,	\
			.parent_cnt = 1,		\
		},					\
		.freq = _freq,				\
		.mult = _mult,				\
		.div = _div,				\
		.fixed_flags = _flags,			\
	}

#endif /* __AW_CLK_H__ */
