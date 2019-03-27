/***********************license start***************
 * Copyright (c) 2003-2010  Cavium Inc. (support@cavium.com). All rights
 * reserved.
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.

 *   * Neither the name of Cavium Inc. nor the names of
 *     its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written
 *     permission.

 * This Software, including technical data, may be subject to U.S. export  control
 * laws, including the U.S. Export Administration Act and its  associated
 * regulations, and may be subject to export or import  regulations in other
 * countries.

 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
 * AND WITH ALL FAULTS AND CAVIUM INC. MAKES NO PROMISES, REPRESENTATIONS OR
 * WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH RESPECT TO
 * THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY REPRESENTATION OR
 * DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT DEFECTS, AND CAVIUM
 * SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY) WARRANTIES OF TITLE,
 * MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A PARTICULAR PURPOSE, LACK OF
 * VIRUSES, ACCURACY OR COMPLETENESS, QUIET ENJOYMENT, QUIET POSSESSION OR
 * CORRESPONDENCE TO DESCRIPTION. THE ENTIRE  RISK ARISING OUT OF USE OR
 * PERFORMANCE OF THE SOFTWARE LIES WITH YOU.
 ***********************license end**************************************/

/**
 * @file
 *
 * Interface to power-throttle control, measurement, and debugging
 * facilities.
 *
 * <hr>$Revision: 70030 $<hr>
 *
 */

#include "cvmx.h"
#include "cvmx-asm.h"
#include "cvmx-coremask.h"
#include "cvmx-power-throttle.h"


#define CVMX_PTH_GET_MASK(len, pos)	\
	((((uint64_t)1 << (len)) - 1) << (pos))

#define CVMX_PTH_AVAILABLE		\
    (cvmx_power_throttle_get_register(0) != (uint64_t)-1)

/**
 * a field of the POWTHROTTLE register
 */
static struct cvmx_power_throttle_rfield_t {
	char	name[16];	/* the field's name */
	int32_t	pos;		/* position of the field's LSb */
	int32_t	len;		/* the field's length */
	int	present;	/* 1 for present */
} cvmx_power_throttle_rfield[] = {
	{"MAXPOW",   56,  8, 0},
	{"POWER" ,   48,  8, 0},
	{"THROTT",   40,  8, 0},
	{"Reserved", 28, 12, 0},
	{"DISTAG",   27,  1, 0},
	{"PERIOD",   24,  3, 0},
	{"POWLIM",   16,  8, 0},
	{"MAXTHR",    8,  8, 0},
	{"MINTHR",    0,  8, 0},
	{"HRMPOWADJ",32,  8, 0},
	{"OVRRD",    28,  1, 0}
};

static uint64_t cvmx_power_throttle_csr_addr(int ppid);

static int cvmx_power_throttle_initialized;

/**
 * @INTERNAL
 * Initialize cvmx_power_throttle_rfield[] based on model.
 */
static void cvmx_power_throttle_init(void)
{
    /*
     * Turn on the fields for a model
     */
    if (OCTEON_IS_MODEL(OCTEON_CN6XXX))
    {
        int i;
	struct cvmx_power_throttle_rfield_t *p;

        for (i = 0; i < CVMX_PTH_INDEX_MAX; i++)
	    cvmx_power_throttle_rfield[i].present = 1;

        if (OCTEON_IS_MODEL(OCTEON_CN63XX))
	{
	    /*
	     * These fields do not come with o63
	     */
	    p = &cvmx_power_throttle_rfield[CVMX_PTH_INDEX_HRMPOWADJ];
	    p->present = 0;
	    p = &cvmx_power_throttle_rfield[CVMX_PTH_INDEX_OVRRD];
	    p->present = 0;
	}
	else
	{
	    /*
	     * The reserved field shrinks in models newer than o63
	     */
	    p = &cvmx_power_throttle_rfield[CVMX_PTH_INDEX_RESERVED];
	    p->pos = 29;
	    p->len = 3;
	}
    }
}

uint64_t cvmx_power_throttle_get_field(uint64_t r,
    cvmx_power_throttle_field_index_t i)
{
    uint64_t m;
    struct cvmx_power_throttle_rfield_t *p;

    assert(i < CVMX_PTH_INDEX_MAX);
    p = &cvmx_power_throttle_rfield[i];
    if (!p->present)
        return (uint64_t) -1;
    m = CVMX_PTH_GET_MASK(p->len, p->pos);
    
    return((r & m) >> p->pos);
}

/**
 * @INTERNAL
 * Set the i'th field of power-throttle register r to v.
 */
static int cvmx_power_throttle_set_field(int i, uint64_t r, uint64_t v)
{
    if (OCTEON_IS_MODEL(OCTEON_CN6XXX))
    {
        uint64_t m;
        struct cvmx_power_throttle_rfield_t *p;

        assert(i < CVMX_PTH_INDEX_MAX);

        p = &cvmx_power_throttle_rfield[i];
        m = CVMX_PTH_GET_MASK(p->len, p->pos);

        return((~m & r) | ((v << p->pos) & m));
    }
    return 0;
}

/**
 * @INTERNAL
 * Set the POWLIM field as percentage% of the MAXPOW field in r.
 */
static uint64_t cvmx_power_throttle_set_powlim(int ppid,
    uint8_t percentage)
{
    if (OCTEON_IS_MODEL(OCTEON_CN6XXX))
    {
        uint64_t t, csr_addr, r;

        assert(percentage < 101);
        csr_addr = cvmx_power_throttle_csr_addr(ppid);
        r = cvmx_read_csr(csr_addr);

        t = cvmx_power_throttle_get_field(r, CVMX_PTH_INDEX_MAXPOW);
	if (!OCTEON_IS_MODEL(OCTEON_CN63XX))
	{
	    uint64_t s;

	    s = cvmx_power_throttle_get_field(r, CVMX_PTH_INDEX_HRMPOWADJ);
	    assert(t > s);
	    t = t - s;
	}

	t = percentage * t / 100;
        r = cvmx_power_throttle_set_field(CVMX_PTH_INDEX_POWLIM, r, t);

        cvmx_write_csr(csr_addr, r);
        return r;
    }
    return 0;
}

/**
 * @INTERNAL
 * Given ppid, calculate its PowThrottle register's L2C_COP0_MAP CSR
 * address. (ppid == PTH_PPID_BCAST is for broadcasting)
 */
static uint64_t cvmx_power_throttle_csr_addr(int ppid)
{
    if (OCTEON_IS_MODEL(OCTEON_CN6XXX))
    {
        uint64_t csr_addr, reg_num, reg_reg, reg_sel;

        assert(ppid < CVMX_MAX_CORES);

        /*
         * register 11 selection 6
         */
        reg_reg = 11;
        reg_sel = 6;
        reg_num = (ppid << 8) + (reg_reg << 3) + reg_sel;
        csr_addr = CVMX_L2C_COP0_MAPX(0) + ((reg_num) << 3);

        return csr_addr;
    }
    return 0;
}

int cvmx_power_throttle_self(uint8_t percentage)
{
    if (!CVMX_PTH_AVAILABLE)
        return -1;

    if (cvmx_power_throttle_set_powlim(cvmx_get_core_num(),
        percentage) == 0)
	return -1;

    return 0;
}

int cvmx_power_throttle(uint8_t percentage, uint64_t coremask)
{
    int ppid;
    int ret;

    if (!CVMX_PTH_AVAILABLE)
        return -1;

    ret = 0;
    for (ppid = 0; ppid < CVMX_MAX_CORES; ppid++)
    {
        if ((((uint64_t) 1) << ppid) & coremask)
	{
            if (cvmx_power_throttle_set_powlim(ppid, percentage) == 0)
	        ret = -2;
        }
    }

    return ret;
}

int cvmx_power_throttle_bmp(uint8_t percentage, struct cvmx_coremask *pcm)
{
    int ppid;
    int ret;

    if (!CVMX_PTH_AVAILABLE)
        return -1;

    ret = 0;
    CVMX_COREMASK_FOR_EACH_CORE_BEGIN(pcm, ppid)
    {
        if (cvmx_power_throttle_set_powlim(ppid, percentage) == 0)
	    ret = -2;
    } CVMX_COREMASK_FOR_EACH_CORE_END;

    return ret;
}

uint64_t cvmx_power_throttle_get_register(int ppid)
{
    uint64_t csr_addr;

    if (!cvmx_power_throttle_initialized)
    {
	cvmx_power_throttle_init();
	cvmx_power_throttle_initialized = 1;
    }

    csr_addr = cvmx_power_throttle_csr_addr(ppid);

    if (csr_addr == 0)
        return -1;

    return cvmx_read_csr(csr_addr);
}
