/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES */
#ifndef __NOHZ_TICK_TEST_H__
#define __NOHZ_TICK_TEST_H__

enum nohz_phase {
	NOHZ_PHASE_INF,
	NOHZ_PHASE_FINITE,
	NOHZ_PHASE_LAZY_ENQ,
	NOHZ_PHASE_LAZY_KICK,
};

#endif /* __NOHZ_TICK_TEST_H__ */
