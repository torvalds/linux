/*-
 * Copyright (c) 2016 Michal Meloun <mmel@FreeBSD.org>
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

#ifndef _TEGRA_EFUSE_H_

enum tegra_revision {
	TEGRA_REVISION_UNKNOWN = 0,
	TEGRA_REVISION_A01,
	TEGRA_REVISION_A02,
	TEGRA_REVISION_A03,
	TEGRA_REVISION_A03p,
	TEGRA_REVISION_A04,
};

struct tegra_sku_info {
	u_int chip_id;
	u_int sku_id;
	u_int cpu_process_id;
	u_int cpu_speedo_id;
	u_int cpu_speedo_value;
	u_int cpu_iddq_value;
	u_int soc_process_id;
	u_int soc_speedo_id;
	u_int soc_speedo_value;
	u_int soc_iddq_value;
	u_int gpu_process_id;
	u_int gpu_speedo_id;
	u_int gpu_speedo_value;
	u_int gpu_iddq_value;
	enum tegra_revision revision;
};

extern struct tegra_sku_info tegra_sku_info;
uint32_t tegra_fuse_read_4(int addr);

#endif /* _TEGRA_EFUSE_H_ */
