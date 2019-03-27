/*-
 * Copyright (c) 2015,2016 Annapurna Labs Ltd. and affiliates
 * All rights reserved.
 *
 * Developed by Semihalf.
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

/**
 *  Ethernet
 *  @{
 * @file   al_init_eth_kr.h
 *
 * @brief auto-negotiation and link training activation sequence
 *
 *
 */

#ifndef __AL_INIT_ETH_KR_H__
#define __AL_INIT_ETH_KR_H__

#include <al_hal_eth_kr.h>
#include <al_serdes.h>

/**
 * execute Auto-negotiation process
 *
 * @param adapter pointer to the private structure
 * @param serdes_obj pointer to serdes private structure
 * @param grp serdes's group
 * @param lane serdes's lane
 * @param an_adv pointer to the AN Advertisement Registers structure
 *        when NULL, the registers will not be updated.
 * @param partner_adv pointer to the AN Advertisement received from the lp
 *
 * @return 0 on success. otherwise on failure.
 */
int al_eth_an_lt_execute(struct al_hal_eth_adapter *adapter,
			 struct al_serdes_grp_obj *serdes_obj,
			 enum al_serdes_lane	lane,
			 struct al_eth_an_adv	*an_adv,
			 struct al_eth_an_adv   *partner_adv);

#endif /*__AL_INIT_ETH_KR_H__*/
