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
 * Support library for the SPI4000 card
 *
 * <hr>$Revision: 70030 $<hr>
 */
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
#include <linux/module.h>
#include <asm/octeon/cvmx.h>
#include <asm/octeon/cvmx-spi.h>
#include <asm/octeon/cvmx-twsi.h>
#include <asm/octeon/cvmx-gmxx-defs.h>
#else
#include "cvmx.h"
#include "cvmx-spi.h"
#include "cvmx-twsi.h"
#endif

/* If someone is using an old config, make the SPI4000 act like RGMII for backpressure */
#ifndef CVMX_HELPER_DISABLE_SPI4000_BACKPRESSURE
#ifndef CVMX_HELPER_DISABLE_RGMII_BACKPRESSURE
#define CVMX_HELPER_DISABLE_RGMII_BACKPRESSURE 0
#endif
#define CVMX_HELPER_DISABLE_SPI4000_BACKPRESSURE CVMX_HELPER_DISABLE_RGMII_BACKPRESSURE
#endif

#define SPI4000_READ_ADDRESS_HIGH   0xf0
#define SPI4000_READ_ADDRESS_LOW    0xf1
#define SPI4000_WRITE_ADDRESS_HIGH  0xf2
#define SPI4000_WRITE_ADDRESS_LOW   0xf3
#define SPI4000_READ_DATA0          0xf4    /* High byte */
#define SPI4000_READ_DATA1          0xf5
#define SPI4000_READ_DATA2          0xf6
#define SPI4000_READ_DATA3          0xf7    /* Low byte */
#define SPI4000_WRITE_DATA0         0xf8    /* High byte */
#define SPI4000_WRITE_DATA1         0xf9
#define SPI4000_WRITE_DATA2         0xfa
#define SPI4000_WRITE_DATA3         0xfb    /* Low byte */
#define SPI4000_DO_READ             0xfc    /* Issue a read, returns read status */
#define SPI4000_GET_READ_STATUS     0xfd    /* 0xff: initial state, 2: Read failed, 1: Read pending, 0: Read success */
#define SPI4000_DO_WRITE            0xfe    /* Issue a write, returns write status */
#define SPI4000_GET_WRITE_STATUS    0xff    /* 0xff: initial state, 6: Write failed, 5: Write pending, 4: Write success */
#define SPI4000_TWSI_ID(interface)  (0x66 + interface)

/* MDI Single Command (register 0x680) */
typedef union
{
    uint32_t u32;
    struct
    {
        uint32_t    reserved_21_31  : 11;
        uint32_t    mdi_command     : 1; /**< Performs an MDIO access. When set, this bit
                                            self clears upon completion of the access. */
        uint32_t    reserved_18_19  : 2;
        uint32_t    op_code         : 2; /**< MDIO Op Code
                                            00 = Reserved
                                            01 = Write Access
                                            10 = Read Access
                                            11 = Reserved */
        uint32_t    reserved_13_15  : 3;
        uint32_t    phy_address     : 5; /**< Address of external PHY device */
        uint32_t    reserved_5_7    : 3;
        uint32_t    reg_address     : 5; /**< Address of register within external PHY */
    } s;
} mdio_single_command_t;


static CVMX_SHARED int interface_is_spi4000[2] = {0,0};


/**
 * @INTERNAL
 * Write data to the specified SPI4000 address
 *
 * @param interface Interface the SPI4000 is on. (0 or 1)
 * @param address   Address to write to
 * @param data      Data to write
 */
static void __cvmx_spi4000_write(int interface, int address, uint32_t data)
{
    int status;
    cvmx_twsix_write_ia(0, SPI4000_TWSI_ID(interface), SPI4000_WRITE_ADDRESS_HIGH, 2, 1, address);
    cvmx_twsix_write_ia(0, SPI4000_TWSI_ID(interface), SPI4000_WRITE_DATA0, 4, 1, data);

    status = cvmx_twsi_read8(SPI4000_TWSI_ID(interface), SPI4000_DO_WRITE);
    while ((status == 5) || (status == 0xff))
        status = cvmx_twsi_read8(SPI4000_TWSI_ID(interface), SPI4000_GET_WRITE_STATUS);

    if (status != 4)
        cvmx_dprintf("SPI4000: write failed with status=0x%x\n", status);
}


/**
 * @INTERNAL
 * Read data from the SPI4000.
 *
 * @param interface Interface the SPI4000 is on. (0 or 1)
 * @param address   Address to read from
 *
 * @return Value at the specified address
 */
static uint32_t __cvmx_spi4000_read(int interface, int address)
{
    int status;
    uint64_t data;

    cvmx_twsix_write_ia(0, SPI4000_TWSI_ID(interface), SPI4000_READ_ADDRESS_HIGH, 2, 1, address);

    status = cvmx_twsi_read8(SPI4000_TWSI_ID(interface), SPI4000_DO_READ);
    while ((status == 1) || (status == 0xff))
        status = cvmx_twsi_read8(SPI4000_TWSI_ID(interface), SPI4000_GET_READ_STATUS);

    if (status)
    {
        cvmx_dprintf("SPI4000: read failed with %d\n", status);
        return 0;
    }

    status = cvmx_twsix_read_ia(0, SPI4000_TWSI_ID(interface), SPI4000_READ_DATA0, 4, 1, &data);
    if (status != 4)
    {
        cvmx_dprintf("SPI4000: read failed with %d\n", status);
        return 0;
    }

    return data;
}


/**
 * @INTERNAL
 * Write to a PHY using MDIO on the SPI4000
 *
 * @param interface Interface the SPI4000 is on. (0 or 1)
 * @param port      SPI4000 RGMII port to write to. (0-9)
 * @param location  MDIO register to write
 * @param val       Value to write
 */
static void __cvmx_spi4000_mdio_write(int interface, int port, int location, int val)
{
    static int last_value=-1;
    mdio_single_command_t mdio;

    mdio.u32 = 0;
    mdio.s.mdi_command = 1;
    mdio.s.op_code = 1;
    mdio.s.phy_address = port;
    mdio.s.reg_address = location;

    /* Since the TWSI accesses are very slow, don't update the write value
        if it is the same as the last value */
    if (val != last_value)
    {
        last_value = val;
        __cvmx_spi4000_write(interface, 0x0681, val);
    }

    __cvmx_spi4000_write(interface, 0x0680, mdio.u32);
}


/**
 * @INTERNAL
 * Read from a PHY using MDIO on the SPI4000
 *
 * @param interface Interface the SPI4000 is on. (0 or 1)
 * @param port      SPI4000 RGMII port to read from. (0-9)
 * @param location  MDIO register to read
 * @return The MDI read result
 */
static int __cvmx_spi4000_mdio_read(int interface, int port, int location)
{
    mdio_single_command_t mdio;

    mdio.u32 = 0;
    mdio.s.mdi_command = 1;
    mdio.s.op_code = 2;
    mdio.s.phy_address = port;
    mdio.s.reg_address = location;
    __cvmx_spi4000_write(interface, 0x0680, mdio.u32);

    do
    {
        mdio.u32 = __cvmx_spi4000_read(interface, 0x0680);
    } while (mdio.s.mdi_command);

    return __cvmx_spi4000_read(interface, 0x0681) >> 16;
}


/**
 * @INTERNAL
 * Configure the SPI4000 MACs
 */
static void __cvmx_spi4000_configure_mac(int interface)
{
    int port;
    // IXF1010 configuration
    // ---------------------
    //
    // Step 1: Apply soft reset to TxFIFO and MAC
    //         MAC soft reset register. address=0x505
    //         TxFIFO soft reset. address=0x620
    __cvmx_spi4000_write(interface, 0x0505, 0x3ff);  // reset all the MACs
    __cvmx_spi4000_write(interface, 0x0620, 0x3ff);  // reset the TX FIFOs

    //         Global address and Configuration Register. address=0x500
    //
    // Step 2: Apply soft reset to RxFIFO and SPI.
    __cvmx_spi4000_write(interface, 0x059e, 0x3ff);  // reset the RX FIFOs

    // Step 3a: Take the MAC out of softreset
    //          MAC soft reset register. address=0x505
    __cvmx_spi4000_write(interface, 0x0505, 0x0);    // reset all the MACs

    // Step 3b: De-assert port enables.
    //          Global address and Configuration Register. address=0x500
    __cvmx_spi4000_write(interface, 0x0500, 0x0);    // disable all ports

    // Step 4: Assert Clock mode change En.
    //         Clock and interface mode Change En. address=Serdes base + 0x14
    //         Serdes (Serializer/de-serializer). address=0x780
    //         [Can't find this one]

    for (port=0; port < 10; port++)
    {
        int port_offset = port << 7;

        // Step 5: Set MAC interface mode GMII speed.
        //         MAC interface mode and RGMII speed register.
        //             address=port_index+0x10
        //
        //         OUT port_index+0x10, 0x07     //RGMII 1000 Mbps operation.
        __cvmx_spi4000_write(interface, port_offset | 0x0010, 0x3);

        // Set the max packet size to 16383 bytes, including the CRC
        __cvmx_spi4000_write(interface, port_offset | 0x000f, 0x3fff);

        // Step 6: Change Interface to Copper mode
        //         Interface mode register. address=0x501
        //         [Can't find this]

        // Step 7: MAC configuration
        //         Station address configuration.
        //         Source MAC address low register. Source MAC address 31-0.
        //             address=port_index+0x00
        //         Source MAC address high register. Source MAC address 47-32.
        //             address=port_index+0x01
        //         where Port index is 0x0 to 0x5.
        //         This address is inserted in the source address filed when
        //         transmitting pause frames, and is also used to compare against
        //         unicast pause frames at the receiving side.
        //
        //         OUT port_index+0x00, source MAC address low.
        __cvmx_spi4000_write(interface, port_offset | 0x0000, 0x0000);
        //         OUT port_index+0x01, source MAC address high.
        __cvmx_spi4000_write(interface, port_offset | 0x0001, 0x0000);

        // Step 8: Set desired duplex mode
        //         Desired duplex register. address=port_index+0x02
        //         [Reserved]

        // Step 9: Other configuration.
        //         FC Enable Register.             address=port_index+0x12
        //         Discard Unknown Control Frame.  address=port_index+0x15
        //         Diverse config write register.  address=port_index+0x18
        //         RX Packet Filter register.      address=port_index+0x19
        //
        // Step 9a: Tx FD FC Enabled / Rx FD FC Enabled
        if (CVMX_HELPER_DISABLE_SPI4000_BACKPRESSURE)
            __cvmx_spi4000_write(interface, port_offset | 0x0012, 0);
        else
            __cvmx_spi4000_write(interface, port_offset | 0x0012, 0x7);

        // Step 9b: Discard unknown control frames
        __cvmx_spi4000_write(interface, port_offset | 0x0015, 0x1);

        // Step 9c: Enable auto-CRC and auto-padding
        __cvmx_spi4000_write(interface, port_offset | 0x0018, 0x11cd); //??

        // Step 9d: Drop bad CRC / Drop Pause / No DAF
        __cvmx_spi4000_write(interface, port_offset | 0x0019, 0x00);
    }

    // Step 9d: Drop frames
    __cvmx_spi4000_write(interface, 0x059f, 0x03ff);

    for (port=0; port < 10; port++)
    {
        // Step 9e: Set the TX FIFO marks
        __cvmx_spi4000_write(interface, port + 0x0600, 0x0900); // TXFIFO High watermark
        __cvmx_spi4000_write(interface, port + 0x060a, 0x0800); // TXFIFO Low watermark
        __cvmx_spi4000_write(interface, port + 0x0614, 0x0380); // TXFIFO threshold
    }

    // Step 12: De-assert RxFIFO and SPI Rx/Tx reset
    __cvmx_spi4000_write(interface, 0x059e, 0x0);    // reset the RX FIFOs

    // Step 13: De-assert TxFIFO and MAC reset
    __cvmx_spi4000_write(interface, 0x0620, 0x0);    // reset the TX FIFOs

    // Step 14: Assert port enable
    //          Global address and Configuration Register. address=0x500
    __cvmx_spi4000_write(interface, 0x0500, 0x03ff); // enable all ports

    // Step 15: Disable loopback
    //          [Can't find this one]
}


/**
 * @INTERNAL
 * Configure the SPI4000 PHYs
 */
static void __cvmx_spi4000_configure_phy(int interface)
{
    int port;

    /* We use separate loops below since it allows us to save a write
        to the SPI4000 for each repeated value. This adds up to a couple
        of seconds */

    /* Update the link state before resets. It takes a while for the links to
        come back after the resets. Most likely they'll come back the same as
        they are now */
    for (port=0; port < 10; port++)
        cvmx_spi4000_check_speed(interface, port);
    /* Enable RGMII DELAYS for TX_CLK and RX_CLK (see spec) */
    for (port=0; port < 10; port++)
        __cvmx_spi4000_mdio_write(interface, port, 0x14, 0x00e2);
    /* Advertise pause and 100 Full Duplex. Don't advertise half duplex or 10Mbpa */
    for (port=0; port < 10; port++)
        __cvmx_spi4000_mdio_write(interface, port, 0x4, 0x0d01);
    /* Enable PHY reset */
    for (port=0; port < 10; port++)
        __cvmx_spi4000_mdio_write(interface, port, 0x0, 0x9140);
}


/**
 * Poll all the SPI4000 port and check its speed
 *
 * @param interface Interface the SPI4000 is on
 * @param port      Port to poll (0-9)
 * @return Status of the port. 0=down. All other values the port is up.
 */
cvmx_gmxx_rxx_rx_inbnd_t cvmx_spi4000_check_speed(int interface, int port)
{
    static int phy_status[10] = {0,};
    cvmx_gmxx_rxx_rx_inbnd_t link;
    int read_status;

    link.u64 = 0;

    if (!interface_is_spi4000[interface])
        return link;
    if (port>=10)
        return link;

    /* Register 0x11: PHY Specific Status Register
         Register   Function         Setting                     Mode   HW Rst SW Rst Notes
                                                                 RO     00     Retain note
         17.15:14   Speed            11 = Reserved
                                                                                      17.a
                                     10 = 1000 Mbps
                                     01 = 100 Mbps
                                     00 = 10 Mbps
         17.13      Duplex           1 = Full-duplex             RO     0      Retain note
                                     0 = Half-duplex                                  17.a
         17.12      Page Received    1 = Page received           RO, LH 0      0
                                     0 = Page not received
                                     1 = Resolved                RO     0      0      note
         17.11      Speed and
                                     0 = Not resolved                                 17.a
                    Duplex
                    Resolved
         17.10      Link (real time) 1 = Link up                 RO     0      0
                                     0 = Link down
                                                                 RO     000    000    note
                                     000 = < 50m
         17.9:7     Cable Length
                                     001 = 50 - 80m                                   17.b
                    (100/1000
                                     010 = 80 - 110m
                    modes only)
                                     011 = 110 - 140m
                                     100 = >140m
         17.6       MDI Crossover    1 = MDIX                    RO     0      0      note
                    Status           0 = MDI                                          17.a
         17.5       Downshift Sta-   1 = Downshift               RO     0      0
                    tus              0 = No Downshift
         17.4       Energy Detect    1 = Sleep                   RO     0      0
                    Status           0 = Active
         17.3       Transmit Pause   1 = Transmit pause enabled  RO     0      0      note17.
                    Enabled          0 = Transmit pause disabled                      a, 17.c
         17.2       Receive Pause    1 = Receive pause enabled   RO     0      0      note17.
                    Enabled          0 = Receive pause disabled                       a, 17.c
         17.1       Polarity (real   1 = Reversed                RO     0      0
                    time)            0 = Normal
         17.0       Jabber (real     1 = Jabber                  RO     0      Retain
                    time)            0 = No jabber
    */
    read_status = __cvmx_spi4000_mdio_read(interface, port, 0x11);
    if ((read_status & (1<<10)) == 0)
        read_status = 0; /* If the link is down, force zero */
    else
        read_status &= 0xe400; /* Strip off all the don't care bits */
    if (read_status != phy_status[port])
    {
        phy_status[port] = read_status;
        if (read_status & (1<<10))
        {
            /* If the link is up, we need to set the speed based on the PHY status */
            if (read_status & (1<<15))
                __cvmx_spi4000_write(interface, (port<<7) | 0x0010, 0x3); /* 1Gbps */
            else
                __cvmx_spi4000_write(interface, (port<<7) | 0x0010, 0x1); /* 100Mbps */
        }
        else
        {
            /* If the link is down, force 1Gbps so TX traffic dumps fast */
            __cvmx_spi4000_write(interface, (port<<7) | 0x0010, 0x3); /* 1Gbps */
        }
    }

    if (read_status & (1<<10))
    {
        link.s.status = 1; /* Link up */
        if (read_status & (1<<15))
            link.s.speed = 2;
        else
            link.s.speed = 1;
    }
    else
    {
        link.s.speed = 2; /* Use 1Gbps when down */
        link.s.status = 0; /* Link Down */
    }
    link.s.duplex = ((read_status & (1<<13)) != 0);

    return link;
}
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(cvmx_spi4000_check_speed);
#endif


/**
 * Return non-zero if the SPI interface has a SPI4000 attached
 *
 * @param interface SPI interface the SPI4000 is connected to
 *
 * @return
 */
int cvmx_spi4000_is_present(int interface)
{
    if (!(OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
        return 0;
    // Check for the presence of a SPI4000. If it isn't there,
    // these writes will timeout.
    if (cvmx_twsi_write8(SPI4000_TWSI_ID(interface), SPI4000_WRITE_ADDRESS_HIGH, 0))
        return 0;
    if (cvmx_twsi_write8(SPI4000_TWSI_ID(interface), SPI4000_WRITE_ADDRESS_LOW, 0))
        return 0;
    interface_is_spi4000[interface] = 1;
    return 1;
}


/**
 * Initialize the SPI4000 for use
 *
 * @param interface SPI interface the SPI4000 is connected to
 */
int cvmx_spi4000_initialize(int interface)
{
    if (!cvmx_spi4000_is_present(interface))
        return -1;

    __cvmx_spi4000_configure_mac(interface);
    __cvmx_spi4000_configure_phy(interface);
    return 0;
}

