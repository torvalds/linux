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





/* This file contains support functions for the Cortina IXF18201 SPI->XAUI dual
** MAC.  The IXF18201 has dual SPI and dual XAUI interfaces to provide 2 10 gigabit
** interfaces.
** This file supports the EBT5810 evaluation board.  To support a different board,
** the 16 bit read/write functions would need to be customized for that board, and the
** IXF18201 may need to be initialized differently as well.
**
** The IXF18201 and Octeon are configured for 2 SPI channels per interface (ports 0/1, and 16/17).
** Ports 0 and 16 are the ports that are connected to the XAUI MACs (which are connected to the SFP+ modules)
** Ports 1 and 17 are connected to the hairpin loopback port on the IXF SPI interface.  All packets sent out
** of these ports are looped back the same port they were sent on.  The loopback ports are always enabled.
**
** The MAC address filtering on the IXF is not enabled.  Link up/down events are not detected, only SPI status
** is monitored by default, which is independent of the XAUI/SFP+ link status.
**
**
*/
#include "cvmx.h"
#include "cvmx-swap.h"





#define PAL_BASE            (1ull << 63 | 0x1d030000)
#define IXF_ADDR_HI         (PAL_BASE + 0xa)
#define IXF_ADDR_LO         (PAL_BASE + 0xb)
#define IXF_ADDR_16         IXF_ADDR_HI         /* 16 bit access */

#define IXF_WR_DATA_HI      (PAL_BASE + 0xc)
#define IXF_WR_DATA_LO      (PAL_BASE + 0xd)
#define IXF_WR_DATA_16      IXF_WR_DATA_HI

#define IXF_RD_DATA_HI      (PAL_BASE + 0x10)
#define IXF_RD_DATA_LO      (PAL_BASE + 0x11)
#define IXF_RD_DATA_16      IXF_RD_DATA_HI

#define IXF_TRANS_TYPE      (PAL_BASE + 0xe)
#define IXF_TRANS_STATUS    (PAL_BASE + 0xf)


uint16_t cvmx_ixf18201_read16(uint16_t reg_addr)
{
    cvmx_write64_uint16(IXF_ADDR_16, reg_addr);
    cvmx_write64_uint8(IXF_TRANS_TYPE, 1);  // Do read
    cvmx_wait(800000);

    /* Read result */
    return(cvmx_read64_uint16(IXF_RD_DATA_16));
}

void cvmx_ixf18201_write16(uint16_t reg_addr, uint16_t data)
{
    cvmx_write64_uint16(IXF_ADDR_16, reg_addr);
    cvmx_write64_uint16(IXF_WR_DATA_16, data);
    cvmx_write64_uint8(IXF_TRANS_TYPE, 0);
    cvmx_wait(800000);
}



uint32_t cvmx_ixf18201_read32(uint16_t reg_addr)
{
    uint32_t hi, lo;

    if (reg_addr & 0x1)
    {
        return(0xdeadbeef);
    }
    lo = cvmx_ixf18201_read16(reg_addr);
    hi = cvmx_ixf18201_read16(reg_addr + 1);
    return((hi << 16) | lo);
}
void cvmx_ixf18201_write32(uint16_t reg_addr, uint32_t data)
{
    uint16_t hi, lo;

    if (reg_addr & 0x1)
    {
        return;
    }
    lo = data & 0xFFFF;
    hi = data >> 16;
    cvmx_ixf18201_write16(reg_addr, lo);
    cvmx_ixf18201_write16(reg_addr + 1, hi);

}


#define IXF_REG_MDI_CMD_ADDR1   0x310E
#define IXF_REG_MDI_RD_WR1      0x3110
void cvmx_ixf18201_mii_write(int mii_addr, int mmd, uint16_t reg, uint16_t val)
{
    uint32_t cmd_val = 0;


    cmd_val = reg;
    cmd_val |= 0x0 << 26;  // Set address operation
    cmd_val |= (mii_addr & 0x1f) << 21;  // Set PHY addr
    cmd_val |= (mmd & 0x1f) << 16;  // Set MMD
    cmd_val |= 1 << 30;   // Do operation
    cmd_val |= 1 << 31;   // enable in progress bit



    /* Set up address */
    cvmx_ixf18201_write32(IXF_REG_MDI_CMD_ADDR1, cmd_val);

    while (cvmx_ixf18201_read32(IXF_REG_MDI_CMD_ADDR1) & ( 1 << 30))
        ;  /* Wait for operation to complete */


    cvmx_ixf18201_write32(IXF_REG_MDI_RD_WR1, val);

    /* Do read operation */
    cmd_val = 0;
    cmd_val |= 0x1 << 26;  // Set write operation
    cmd_val |= (mii_addr & 0x1f) << 21;  // Set PHY addr
    cmd_val |= (mmd & 0x1f) << 16;  // Set MMD
    cmd_val |= 1 << 30;   // Do operation
    cmd_val |= 1 << 31;   // enable in progress bit
    cvmx_ixf18201_write32(IXF_REG_MDI_CMD_ADDR1, cmd_val);

    while (cvmx_ixf18201_read32(IXF_REG_MDI_CMD_ADDR1) & ( 1 << 30))
        ;  /* Wait for operation to complete */


}


int cvmx_ixf18201_mii_read(int mii_addr, int mmd, uint16_t reg)
{
    uint32_t cmd_val = 0;


    cmd_val = reg;
    cmd_val |= 0x0 << 26;  // Set address operation
    cmd_val |= (mii_addr & 0x1f) << 21;  // Set PHY addr
    cmd_val |= (mmd & 0x1f) << 16;  // Set MMD
    cmd_val |= 1 << 30;   // Do operation
    cmd_val |= 1 << 31;   // enable in progress bit



    /* Set up address */
    cvmx_ixf18201_write32(IXF_REG_MDI_CMD_ADDR1, cmd_val);

    while (cvmx_ixf18201_read32(IXF_REG_MDI_CMD_ADDR1) & ( 1 << 30))
        ;  /* Wait for operation to complete */

    /* Do read operation */
    cmd_val = 0;
    cmd_val |= 0x3 << 26;  // Set read operation
    cmd_val |= (mii_addr & 0x1f) << 21;  // Set PHY addr
    cmd_val |= (mmd & 0x1f) << 16;  // Set MMD
    cmd_val |= 1 << 30;   // Do operation
    cmd_val |= 1 << 31;   // enable in progress bit
    cvmx_ixf18201_write32(IXF_REG_MDI_CMD_ADDR1, cmd_val);

    while (cvmx_ixf18201_read32(IXF_REG_MDI_CMD_ADDR1) & ( 1 << 30))
        ;  /* Wait for operation to complete */

    cmd_val = cvmx_ixf18201_read32(IXF_REG_MDI_RD_WR1);

    return(cmd_val >> 16);

}



int cvmx_ixf18201_init(void)
{
    int index;  /* For indexing the two 'ports' on ixf */
    int offset;

    /* Reset IXF, and take all blocks out of reset */

/*
Initializing...
PP0:~CONSOLE-> Changing register value, addr 0x0003, old: 0x0000, new: 0x0001
PP0:~CONSOLE-> Changing register value, addr 0x0003, old: 0x0001, new: 0x0000
PP0:~CONSOLE->  **** LLM201(Lochlomond) Driver loaded ****
PP0:~CONSOLE->  LLM201 Driver - Released on Tue Aug 28 09:51:30 2007.
PP0:~CONSOLE-> retval is: 0
PP0:~CONSOLE-> Changing register value, addr 0x0003, old: 0x0000, new: 0x0001
PP0:~CONSOLE-> Changing register value, addr 0x0003, old: 0x0001, new: 0x0000
PP0:~CONSOLE-> Brought all blocks out of reset
PP0:~CONSOLE-> Getting default config.
*/


    cvmx_ixf18201_write16(0x0003, 0x0001);
    cvmx_ixf18201_write16(0x0003, 0);

    /*
PP0:~CONSOLE-> Changing register value, addr 0x0000, old: 0x4014, new: 0x4010
PP0:~CONSOLE-> Changing register value, addr 0x0000, old: 0x4010, new: 0x4014
PP0:~CONSOLE-> Changing register value, addr 0x0004, old: 0x01ff, new: 0x0140
PP0:~CONSOLE-> Changing register value, addr 0x0009, old: 0x007f, new: 0x0000
    */
    cvmx_ixf18201_write16(0x0000, 0x4010);
    cvmx_ixf18201_write16(0x0000, 0x4014);
    cvmx_ixf18201_write16(0x0004, 0x0140);
    cvmx_ixf18201_write16(0x0009, 0);


    /*
PP0:~CONSOLE-> Changing register value, addr 0x000e, old: 0x0000, new: 0x000f
PP0:~CONSOLE-> Changing register value, addr 0x000f, old: 0x0000, new: 0x0004
PP0:~CONSOLE-> Changing register value, addr 0x000f, old: 0x0004, new: 0x0006
PP0:~CONSOLE-> Changing register value, addr 0x000e, old: 0x000f, new: 0x00f0
PP0:~CONSOLE-> Changing register value, addr 0x000f, old: 0x0006, new: 0x0040
PP0:~CONSOLE-> Changing register value, addr 0x000f, old: 0x0040, new: 0x0060
    */
    // skip GPIO, 0xe/0xf


    /*
PP0:~CONSOLE-> Changing register value, addr 0x3100, old: 0x57fb, new: 0x7f7b
PP0:~CONSOLE-> Changing register value, addr 0x3600, old: 0x57fb, new: 0x7f7b
PP0:~CONSOLE-> Changing register value, addr 0x3005, old: 0x8010, new: 0x0040
PP0:~CONSOLE-> Changing register value, addr 0x3006, old: 0x061a, new: 0x0000
PP0:~CONSOLE-> Changing register value, addr 0x3505, old: 0x8010, new: 0x0040
PP0:~CONSOLE-> Changing register value, addr 0x3506, old: 0x061a, new: 0x0000
    */
    for (index = 0; index < 2;index++ )
    {
        offset = 0x500 * index;
        cvmx_ixf18201_write32(0x3100 + offset, 0x47f7b);
        cvmx_ixf18201_write16(0x3005 + offset, 0x0040);
        cvmx_ixf18201_write16(0x3006 + offset, 0);
    }

    /*PP0:~CONSOLE->   *** SPI soft reset ***, block id: 0
PP0:~CONSOLE-> Changing register value, addr 0x3007, old: 0xf980, new: 0xf9c0
PP0:~CONSOLE-> Changing register value, addr 0x3008, old: 0xa6f0, new: 0x36f0
PP0:~CONSOLE-> Changing register value, addr 0x3000, old: 0x0080, new: 0x0060
PP0:~CONSOLE-> Changing register value, addr 0x3002, old: 0x0200, new: 0x0040
PP0:~CONSOLE-> Changing register value, addr 0x3003, old: 0x0100, new: 0x0000
PP0:~CONSOLE-> Changing register value, addr 0x30c2, old: 0x0080, new: 0x0060
PP0:~CONSOLE-> Changing register value, addr 0x300a, old: 0x0800, new: 0x0000
PP0:~CONSOLE-> Changing register value, addr 0x3007, old: 0xf9c0, new: 0x89c0
PP0:~CONSOLE-> Changing register value, addr 0x3016, old: 0x0000, new: 0x0010
PP0:~CONSOLE-> Changing register value, addr 0x3008, old: 0x36f0, new: 0x3610
PP0:~CONSOLE-> Changing register value, addr 0x3012, old: 0x0000, new: 0x0010
PP0:~CONSOLE-> Changing register value, addr 0x3007, old: 0x89c0, new: 0x8980
PP0:~CONSOLE-> Changing register value, addr 0x3008, old: 0x3610, new: 0xa210
PP0:~CONSOLE->

    */


    for (index = 0; index < 2;index++ )
    {
        offset = 0x500 * index;
        int cal_len_min_1 = 0;  /* Calendar length -1.  Must match number
                                ** of ports configured for interface.*/
        cvmx_ixf18201_write16(0x3007 + offset, 0x81c0 | (cal_len_min_1 << 11));
        cvmx_ixf18201_write16(0x3008 + offset, 0x3600 | (cal_len_min_1 << 4));
        cvmx_ixf18201_write16(0x3000 + offset, 0x0060);
        cvmx_ixf18201_write16(0x3002 + offset, 0x0040);
        cvmx_ixf18201_write16(0x3003 + offset, 0x0000);
        cvmx_ixf18201_write16(0x30c2 + offset, 0x0060);
        cvmx_ixf18201_write16(0x300a + offset, 0x0000);
        cvmx_ixf18201_write16(0x3007 + offset, 0x81c0 | (cal_len_min_1 << 11));
        cvmx_ixf18201_write16(0x3016 + offset, 0x0010);
        cvmx_ixf18201_write16(0x3008 + offset, 0x3600 | (cal_len_min_1 << 4));
        cvmx_ixf18201_write16(0x3012 + offset, 0x0010);
        cvmx_ixf18201_write16(0x3007 + offset, 0x8180 | (cal_len_min_1 << 11));
        cvmx_ixf18201_write16(0x3008 + offset, 0xa200 | (cal_len_min_1 << 4));

        cvmx_ixf18201_write16(0x3090 + offset, 0x0301);  /* Enable hairpin loopback */
    }



    /*
PP0:~CONSOLE-> Changing register value, addr 0x0004, old: 0x0140, new: 0x1fff
PP0:~CONSOLE-> Changing register value, addr 0x0009, old: 0x0000, new: 0x007f
PP0:~CONSOLE-> Changing register value, addr 0x310b, old: 0x0004, new: 0xffff
PP0:~CONSOLE-> Changing register value, addr 0x310a, old: 0x7f7b, new: 0xffff

    */

    cvmx_ixf18201_write16(0x0004, 0x1fff);
    cvmx_ixf18201_write16(0x0009, 0x007f);
#if 0
    /* MDI autoscan */
    cvmx_ixf18201_write16(0x310b, 0xffff);
    cvmx_ixf18201_write16(0x310a, 0xffff);
#endif


    /*
    *** 32 bit register, trace only captures part of it...
PP0:~CONSOLE-> Changing register value, addr 0x3100, old: 0x7f7b, new: 0x7f78
PP0:~CONSOLE-> Changing register value, addr 0x3600, old: 0x7f7b, new: 0x7f78
    */

    for (index = 0; index < 2;index++ )
    {
        offset = 0x500 * index;
        cvmx_ixf18201_write32(0x3100 + offset, 0x47f7c); /* Also enable jumbo frames */
        /* Set max packet size to 9600 bytes, max supported by IXF18201 */
        cvmx_ixf18201_write32(0x3114 + offset, 0x25800000);
    }


    cvmx_wait(100000000);

    /* Now reset the PCS blocks in the phy.  This seems to be required after
    ** bringing up the Cortina. */
    cvmx_ixf18201_mii_write(1, 3, 0, 0x8000);
    cvmx_ixf18201_mii_write(5, 3, 0, 0x8000);


    return 1;

}
