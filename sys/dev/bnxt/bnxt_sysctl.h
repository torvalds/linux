/*-
 * Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2016 Broadcom, All Rights Reserved.
 * The term Broadcom refers to Broadcom Limited and/or its subsidiaries
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS'
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "bnxt.h"

int bnxt_init_sysctl_ctx(struct bnxt_softc *softc);
int bnxt_free_sysctl_ctx(struct bnxt_softc *softc);
int bnxt_create_port_stats_sysctls(struct bnxt_softc *softc);
int bnxt_create_tx_sysctls(struct bnxt_softc *softc, int txr);
int bnxt_create_rx_sysctls(struct bnxt_softc *softc, int rxr);
int bnxt_create_ver_sysctls(struct bnxt_softc *softc);
int bnxt_create_nvram_sysctls(struct bnxt_nvram_info *ni);
int bnxt_create_config_sysctls_pre(struct bnxt_softc *softc);
int bnxt_create_config_sysctls_post(struct bnxt_softc *softc);
int bnxt_create_hw_lro_sysctls(struct bnxt_softc *softc);
int bnxt_create_pause_fc_sysctls(struct bnxt_softc *softc);
