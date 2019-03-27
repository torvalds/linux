/*******************************************************************************
Copyright (C) 2013 Annapurna Labs Ltd.

This file is licensed under the terms of the Annapurna Labs' Commercial License
Agreement distributed with the file or available on the software download site.
Recipient shall use the content of this file only on semiconductor devices or
systems developed by or for Annapurna Labs.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*******************************************************************************/

/**
 * @defgroup group_serdes_init SerDes Initialization
 * @ingroup group_serdes SerDes
 * @{
 *
 * @file   al_serdes.h
 *
 */

#ifndef __AL_SERDES_H__
#define __AL_SERDES_H__

#include "al_hal_serdes_interface.h"

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

#ifdef AL_DEV_ID
#define CHECK_ALPINE_V1	(AL_DEV_ID == AL_DEV_ID_ALPINE_V1)
#define CHECK_ALPINE_V2	(AL_DEV_ID == AL_DEV_ID_ALPINE_V2)
#else
#define CHECK_ALPINE_V1	1
#define CHECK_ALPINE_V2	1
#endif

enum al_serdes_group {
	AL_SRDS_GRP_A = 0,
	AL_SRDS_GRP_B,
	AL_SRDS_GRP_C,
	AL_SRDS_GRP_D,
	AL_SRDS_NUM_HSSP_GROUPS,
#if CHECK_ALPINE_V2
	AL_SRDS_GRP_E = AL_SRDS_NUM_HSSP_GROUPS,
	AL_SRDS_NUM_GROUPS,
#else
	AL_SRDS_NUM_GROUPS = AL_SRDS_NUM_HSSP_GROUPS,
#endif
};

int al_serdes_handle_grp_init(
	void __iomem			*serdes_regs_base,
	enum al_serdes_group		grp,
	struct al_serdes_grp_obj	*obj);

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif

/* *INDENT-ON* */
#endif

/** @} end of SERDES group */

