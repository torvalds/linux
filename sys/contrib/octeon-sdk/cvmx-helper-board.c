/***********************license start***************
 * Copyright (c) 2003-2011  Cavium Inc. (support@cavium.com). All rights
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
 * Helper functions to abstract board specific data about
 * network ports from the rest of the cvmx-helper files.
 *
 * <hr>$Revision: 70030 $<hr>
 */
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
#include <linux/module.h>
#include <asm/octeon/cvmx.h>
#include <asm/octeon/cvmx-bootinfo.h>
#include <asm/octeon/cvmx-smix-defs.h>
#include <asm/octeon/cvmx-gmxx-defs.h>
#include <asm/octeon/cvmx-asxx-defs.h>
#include <asm/octeon/cvmx-mdio.h>
#include <asm/octeon/cvmx-helper.h>
#include <asm/octeon/cvmx-helper-util.h>
#include <asm/octeon/cvmx-helper-board.h>
#include <asm/octeon/cvmx-twsi.h>
#else
#include "cvmx.h"
#include "cvmx-app-init.h"
#include "cvmx-sysinfo.h"
#include "cvmx-twsi.h"
#include "cvmx-mdio.h"
#include "cvmx-helper.h"
#include "cvmx-helper-util.h"
#include "cvmx-helper-board.h"
#include "cvmx-gpio.h"
#if !defined(__FreeBSD__) || !defined(_KERNEL)
#ifdef __U_BOOT__
# include <libfdt.h>
#else
# include "libfdt/libfdt.h"
#endif
#endif
#include "cvmx-swap.h"
#endif

/**
 * cvmx_override_board_link_get(int ipd_port) is a function
 * pointer. It is meant to allow customization of the process of
 * talking to a PHY to determine link speed. It is called every
 * time a PHY must be polled for link status. Users should set
 * this pointer to a function before calling any cvmx-helper
 * operations.
 */
CVMX_SHARED cvmx_helper_link_info_t (*cvmx_override_board_link_get)(int ipd_port) = NULL;

#if !defined(CVMX_BUILD_FOR_LINUX_KERNEL) && (!defined(__FreeBSD__) || !defined(_KERNEL))

static void cvmx_retry_i2c_write(int twsi_id, uint8_t dev_addr, uint16_t internal_addr, int num_bytes, int ia_width_bytes, uint64_t data)
{
    int tries = 3;
    int r;
    do {
        r = cvmx_twsix_write_ia(twsi_id, dev_addr, internal_addr, num_bytes, ia_width_bytes, data);
    } while (tries-- > 0 && r < 0);
}

static int __pip_eth_node(const void *fdt_addr, int aliases, int ipd_port)
{
    char name_buffer[20];
    const char*pip_path;
    int pip, iface, eth;
    int interface_num    = cvmx_helper_get_interface_num(ipd_port);
    int interface_index  = cvmx_helper_get_interface_index_num(ipd_port);

    pip_path = fdt_getprop(fdt_addr, aliases, "pip", NULL);
    if (!pip_path)
    {
        cvmx_dprintf("ERROR: pip path not found in device tree\n");
        return -1;
    }
    pip = fdt_path_offset(fdt_addr, pip_path);
    if (pip < 0)
    {
        cvmx_dprintf("ERROR: pip not found in device tree\n");
        return -1;
    }
#ifdef __U_BOOT__
    sprintf(name_buffer, "interface@%d", interface_num);
#else
    snprintf(name_buffer, sizeof(name_buffer), "interface@%d", interface_num);
#endif
    iface =  fdt_subnode_offset(fdt_addr, pip, name_buffer);
    if (iface < 0)
    {
        cvmx_dprintf("ERROR : pip intf %d not found in device tree \n",
                     interface_num);
        return -1;
    }
#ifdef __U_BOOT__
    sprintf(name_buffer, "ethernet@%x", interface_index);
#else
    snprintf(name_buffer, sizeof(name_buffer), "ethernet@%x", interface_index);
#endif
    eth = fdt_subnode_offset(fdt_addr, iface, name_buffer);
    if (eth < 0)
    {
        cvmx_dprintf("ERROR : pip interface@%d ethernet@%d not found in device "
                     "tree\n", interface_num, interface_index);
        return -1;
    }
    return eth;
}

static int __mix_eth_node(const void *fdt_addr, int aliases, int interface_index)
{
    char name_buffer[20];
    const char*mix_path;
    int mix;

#ifdef __U_BOOT__
    sprintf(name_buffer, "mix%d", interface_index);
#else
    snprintf(name_buffer, sizeof(name_buffer), "mix%d", interface_index);
#endif
    mix_path = fdt_getprop(fdt_addr, aliases, name_buffer, NULL);
    if (!mix_path)
    {
        cvmx_dprintf("ERROR: mix%d path not found in device tree\n",interface_index);
    }
    mix = fdt_path_offset(fdt_addr, mix_path);
    if (mix < 0)
    {
        cvmx_dprintf("ERROR: %s not found in device tree\n", mix_path);
        return -1;
    }
    return mix;
}

typedef struct cvmx_phy_info
{
    int phy_addr;
    int direct_connect;
    cvmx_phy_type_t phy_type;
}cvmx_phy_info_t;


static int __mdiobus_addr_to_unit(uint32_t addr)
{
    int unit = (addr >> 7) & 3;
    if (!OCTEON_IS_MODEL(OCTEON_CN68XX))
        unit >>= 1;
    return unit;
}
/**
 * Return the MII PHY address associated with the given IPD
 * port. The phy address is obtained from the device tree.
 *
 * @param ipd_port Octeon IPD port to get the MII address for.
 *
 * @return MII PHY address and bus number or -1.
 */

static cvmx_phy_info_t __get_phy_info_from_dt(int ipd_port)
{
    const void *fdt_addr = CASTPTR(const void *, cvmx_sysinfo_get()->fdt_addr);
    uint32_t *phy_handle;
    int aliases, eth, phy, phy_parent, phandle, ret;
    cvmx_phy_info_t phy_info;
    int mdio_unit=-1;
    const char *phy_comaptible_str;
    uint32_t *phy_addr_ptr;

    phy_info.phy_addr = -1;
    phy_info.direct_connect = -1;
    phy_info.phy_type = (cvmx_phy_type_t) -1;

    if (!fdt_addr)
    {
        cvmx_dprintf("No device tree found.\n");
        return phy_info;
    }
    aliases = fdt_path_offset(fdt_addr, "/aliases");
    if (aliases < 0) {
        cvmx_dprintf("Error: No /aliases node in device tree.\n");
        return phy_info;
    }
    if (ipd_port < 0)
    {
        int interface_index = ipd_port - CVMX_HELPER_BOARD_MGMT_IPD_PORT;
        eth = __mix_eth_node(fdt_addr, aliases, interface_index) ;
    }
    else
    {
        eth = __pip_eth_node(fdt_addr, aliases, ipd_port);
    }
    if (eth < 0 )
    {
        cvmx_dprintf("ERROR : cannot find interface for ipd_port=%d\n", ipd_port);
        return phy_info;
    }
    /* Get handle to phy */
    phy_handle = (uint32_t *) fdt_getprop(fdt_addr, eth, "phy-handle", NULL);
    if (!phy_handle)
    {
        cvmx_dprintf("ERROR : phy handle not found in device tree ipd_port=%d"
                     "\n", ipd_port);
        return phy_info;
    }
    phandle = cvmx_be32_to_cpu(*phy_handle);
    phy = fdt_node_offset_by_phandle(fdt_addr, phandle);
    if (phy < 0)
    {
        cvmx_dprintf("ERROR : cannot find phy for ipd_port=%d ret=%d\n",
                     ipd_port, phy);
        return phy_info;
    }
    phy_comaptible_str = (const char *) fdt_getprop(fdt_addr, phy,
                                                    "compatible", NULL);
    if (!phy_comaptible_str)
    {
        cvmx_dprintf("ERROR : no compatible prop in phy\n");
        return phy_info;
    }
    if (memcmp("marvell", phy_comaptible_str, strlen("marvell")) == 0)
    {
        phy_info.phy_type = MARVELL_GENERIC_PHY;
    }
    else if (memcmp("broadcom", phy_comaptible_str, strlen("broadcom")) == 0)
    {
        phy_info.phy_type = BROADCOM_GENERIC_PHY;
    }
    else
    {
        phy_info.phy_type = -1;
    }

    /* Check if PHY parent is the octeon MDIO bus. Some boards are connected
       though a MUX and for them direct_connect_to_phy will be 0 */
    phy_parent = fdt_parent_offset(fdt_addr, phy);
    if (phy_parent < 0)
    {
        cvmx_dprintf("ERROR : cannot find phy parent for ipd_port=%d ret=%d\n",
                     ipd_port, phy_parent);
        return phy_info;
    }
    ret = fdt_node_check_compatible(fdt_addr, phy_parent,
                                    "cavium,octeon-3860-mdio");
    if (ret == 0)
    {
        phy_info.direct_connect = 1 ;
        uint32_t *mdio_reg_base = (uint32_t *) fdt_getprop(fdt_addr, phy_parent,"reg",0);
        if (mdio_reg_base == 0)
        {
            cvmx_dprintf("ERROR : unable to get reg property in phy mdio\n");
            return phy_info;
        }
        mdio_unit = __mdiobus_addr_to_unit(mdio_reg_base[1]);
        //cvmx_dprintf("phy parent=%s reg_base=%08x unit=%d \n",
        //             fdt_get_name(fdt_addr,phy_parent, NULL), mdio_reg_base[1], mdio_unit);
    }
    else
    {
        phy_info.direct_connect = 0;
        /* The PHY is not directly connected to the Octeon MDIO bus.
           SE doesn't  have abstractions for MDIO MUX or MDIO MUX drivers and
           hence for the non direct cases code will be needed which is
           board specific.
           For now the the MDIO Unit is defaulted to 1.
        */
        mdio_unit = 1;
    }

    phy_addr_ptr = (uint32_t *) fdt_getprop(fdt_addr, phy, "reg", NULL);
    phy_info.phy_addr = cvmx_be32_to_cpu(*phy_addr_ptr) | mdio_unit << 8;
    return phy_info;

}

/**
 * Return the MII PHY address associated with the given IPD
 * port. The phy address is obtained from the device tree.
 *
 * @param ipd_port Octeon IPD port to get the MII address for.
 *
 * @return MII PHY address and bus number or -1.
 */

int cvmx_helper_board_get_mii_address_from_dt(int ipd_port)
{
        cvmx_phy_info_t phy_info = __get_phy_info_from_dt(ipd_port);
        return phy_info.phy_addr;
}
#endif

/**
 * Return the MII PHY address associated with the given IPD
 * port. A result of -1 means there isn't a MII capable PHY
 * connected to this port. On chips supporting multiple MII
 * busses the bus number is encoded in bits <15:8>.
 *
 * This function must be modified for every new Octeon board.
 * Internally it uses switch statements based on the cvmx_sysinfo
 * data to determine board types and revisions. It replies on the
 * fact that every Octeon board receives a unique board type
 * enumeration from the bootloader.
 *
 * @param ipd_port Octeon IPD port to get the MII address for.
 *
 * @return MII PHY address and bus number or -1.
 */
int cvmx_helper_board_get_mii_address(int ipd_port)
{
    /*
     * Board types we have to know at compile-time.
     */
#ifdef OCTEON_BOARD_CAPK_0100ND
    switch (ipd_port) {
    case 0:
	return 2;
    case 1:
	return 3;
    case 2:
	/* XXX Switch PHY?  */
	return -1;
    default:
	return -1;
    }
#endif

    /*
     * For board types we can determine at runtime.
     */
    if (cvmx_sysinfo_get()->board_type == CVMX_BOARD_TYPE_SIM)
        return -1;
#if !defined(CVMX_BUILD_FOR_LINUX_KERNEL) && (!defined(__FreeBSD__) || !defined(_KERNEL))
    if (cvmx_sysinfo_get()->fdt_addr)
    {
        cvmx_phy_info_t phy_info = __get_phy_info_from_dt(ipd_port);
        //cvmx_dprintf("ipd_port=%d phy_addr=%d\n", ipd_port, phy_info.phy_addr);
        if (phy_info.phy_addr >= 0) return phy_info.phy_addr;
    }
#endif
    switch (cvmx_sysinfo_get()->board_type)
    {
        case CVMX_BOARD_TYPE_SIM:
            /* Simulator doesn't have MII */
            return -1;
#if !defined(OCTEON_VENDOR_GEFES)
        case CVMX_BOARD_TYPE_EBT5800:
        case CVMX_BOARD_TYPE_NICPRO2:
#endif
        case CVMX_BOARD_TYPE_EBT3000:
        case CVMX_BOARD_TYPE_THUNDER:
            /* Interface 0 is SPI4, interface 1 is RGMII */
            if ((ipd_port >= 16) && (ipd_port < 20))
                return ipd_port - 16;
            else
                return -1;
        case CVMX_BOARD_TYPE_LANAI2_A:
            if (ipd_port == 0)
                return 0;
            else
                return -1;
        case CVMX_BOARD_TYPE_LANAI2_U:
        case CVMX_BOARD_TYPE_LANAI2_G:
            if (ipd_port == 0)
                return 0x1c;
            else
                return -1;
        case CVMX_BOARD_TYPE_KODAMA:
        case CVMX_BOARD_TYPE_EBH3100:
        case CVMX_BOARD_TYPE_HIKARI:
        case CVMX_BOARD_TYPE_CN3010_EVB_HS5:
        case CVMX_BOARD_TYPE_CN3005_EVB_HS5:
#if !defined(OCTEON_VENDOR_GEFES)
        case CVMX_BOARD_TYPE_CN3020_EVB_HS5:
#endif
            /* Port 0 is WAN connected to a PHY, Port 1 is GMII connected to a
                switch */
            if (ipd_port == 0)
                return 4;
            else if (ipd_port == 1)
                return 9;
            else
                return -1;
        case CVMX_BOARD_TYPE_EBH3000:
            /* Board has dual SPI4 and no PHYs */
            return -1;
        case CVMX_BOARD_TYPE_EBT5810:
            /* Board has 10g PHYs hooked up to the MII controller on the
            ** IXF18201 MAC.  The 10G PHYS use clause 45 MDIO which the CN58XX
            ** does not support. All MII accesses go through the IXF part. */
            return -1;
        case CVMX_BOARD_TYPE_EBH5200:
        case CVMX_BOARD_TYPE_EBH5201:
        case CVMX_BOARD_TYPE_EBT5200:
            /* Board has 2 management ports */
            if ((ipd_port >= CVMX_HELPER_BOARD_MGMT_IPD_PORT) && (ipd_port < (CVMX_HELPER_BOARD_MGMT_IPD_PORT + 2)))
                return ipd_port - CVMX_HELPER_BOARD_MGMT_IPD_PORT;
            /* Board has 4 SGMII ports. The PHYs start right after the MII
                ports MII0 = 0, MII1 = 1, SGMII = 2-5 */
            if ((ipd_port >= 0) && (ipd_port < 4))
                return ipd_port+2;
            else
                return -1;
        case CVMX_BOARD_TYPE_EBH5600:
        case CVMX_BOARD_TYPE_EBH5601:
        case CVMX_BOARD_TYPE_EBH5610:
            /* Board has 1 management port */
            if (ipd_port == CVMX_HELPER_BOARD_MGMT_IPD_PORT)
                return 0;
            /* Board has 8 SGMII ports. 4 connect out, two connect to a switch,
                and 2 loop to each other */
            if ((ipd_port >= 0) && (ipd_port < 4))
                return ipd_port+1;
            else
                return -1;
        case CVMX_BOARD_TYPE_EBT5600:
	    /* Board has 1 management port */
            if (ipd_port == CVMX_HELPER_BOARD_MGMT_IPD_PORT)
                return 0;
	    /* Board has 1 XAUI port connected to a switch.  */
	    return -1;
        case CVMX_BOARD_TYPE_EBB5600:
            {
                static unsigned char qlm_switch_addr = 0;

                /* Board has 1 management port */
                if (ipd_port == CVMX_HELPER_BOARD_MGMT_IPD_PORT)
                    return 0;

                /* Board has 8 SGMII ports. 4 connected QLM1, 4 connected QLM3 */
                if ((ipd_port >= 0) && (ipd_port < 4))
                {
                    if (qlm_switch_addr != 0x3)
                    {
                        qlm_switch_addr = 0x3;  /* QLM1 */
                        cvmx_twsix_write_ia(0, 0x71, 0, 1, 1, qlm_switch_addr);
                        cvmx_wait_usec(11000); /* Let the write complete */
                    }
                    return ipd_port+1 + (1<<8);
                }
                else if ((ipd_port >= 16) && (ipd_port < 20))
                {
                    if (qlm_switch_addr != 0xC)
                    {
                        qlm_switch_addr = 0xC;  /* QLM3 */
                        cvmx_twsix_write_ia(0, 0x71, 0, 1, 1, qlm_switch_addr);
                        cvmx_wait_usec(11000); /* Let the write complete */
                    }
                    return ipd_port-16+1 + (1<<8);
                }
                else
                    return -1;
            }
        case CVMX_BOARD_TYPE_EBB6300:
            /* Board has 2 management ports */
            if ((ipd_port >= CVMX_HELPER_BOARD_MGMT_IPD_PORT) && (ipd_port < (CVMX_HELPER_BOARD_MGMT_IPD_PORT + 2)))
                return ipd_port - CVMX_HELPER_BOARD_MGMT_IPD_PORT + 4;
            if ((ipd_port >= 0) && (ipd_port < 4))
                return ipd_port + 1 + (1<<8);
            else
                return -1;
        case CVMX_BOARD_TYPE_EBB6800:
            /* Board has 1 management ports */
            if (ipd_port == CVMX_HELPER_BOARD_MGMT_IPD_PORT)
                return 6;
            if (ipd_port >= 0x800 && ipd_port < 0x900) /* QLM 0*/
                return 0x101 + ((ipd_port >> 4) & 3); /* SMI 1*/
            if (ipd_port >= 0xa00 && ipd_port < 0xb00) /* QLM 2*/
                return 0x201 + ((ipd_port >> 4) & 3); /* SMI 2*/
            if (ipd_port >= 0xb00 && ipd_port < 0xc00) /* QLM 3*/
                return 0x301 + ((ipd_port >> 4) & 3); /* SMI 3*/
            if (ipd_port >= 0xc00 && ipd_port < 0xd00) /* QLM 4*/
                return 0x001 + ((ipd_port >> 4) & 3); /* SMI 0*/
            return -1;
        case CVMX_BOARD_TYPE_EP6300C:
            if (ipd_port == CVMX_HELPER_BOARD_MGMT_IPD_PORT)
                return 0x01;
            if (ipd_port == CVMX_HELPER_BOARD_MGMT_IPD_PORT+1)
                return 0x02;
#ifdef CVMX_ENABLE_PKO_FUNCTIONS
            {
                int interface = cvmx_helper_get_interface_num(ipd_port);
                int mode = cvmx_helper_interface_get_mode(interface);
                if (mode == CVMX_HELPER_INTERFACE_MODE_XAUI)
                    return ipd_port;
                else if ((ipd_port >= 0) && (ipd_port < 4))
                    return ipd_port + 3;
                else
                    return -1;
            }
#endif
            break;
        case CVMX_BOARD_TYPE_CUST_NB5:
            if (ipd_port == 2)
                return 4;
            else
                return -1;
        case CVMX_BOARD_TYPE_NIC_XLE_4G:
            /* Board has 4 SGMII ports. connected QLM3(interface 1) */
            if ((ipd_port >= 16) && (ipd_port < 20))
                return ipd_port - 16 + 1;
            else
                return -1;
        case CVMX_BOARD_TYPE_NIC_XLE_10G:
        case CVMX_BOARD_TYPE_NIC10E:
            return -1;  /* We don't use clause 45 MDIO for anything */
        case CVMX_BOARD_TYPE_NIC4E:
            if (ipd_port >= 0 && ipd_port <= 3)
                return (ipd_port + 0x1f) & 0x1f;
            else
                return -1;
        case CVMX_BOARD_TYPE_NIC2E:
            if (ipd_port >= 0 && ipd_port <= 1)
                return (ipd_port + 1);
            else
                return -1;
        case CVMX_BOARD_TYPE_REDWING:
	    return -1;  /* No PHYs connected to Octeon */
        case CVMX_BOARD_TYPE_BBGW_REF:
            return -1;  /* No PHYs are connected to Octeon, everything is through switch */
	case CVMX_BOARD_TYPE_CUST_WSX16:
		if (ipd_port >= 0 && ipd_port <= 3)
			return ipd_port;
		else if (ipd_port >= 16 && ipd_port <= 19)
			return ipd_port - 16 + 4;
		else
			return -1;

	/* Private vendor-defined boards.  */
#if defined(OCTEON_VENDOR_LANNER)
	case CVMX_BOARD_TYPE_CUST_LANNER_MR955:
	    /* Interface 1 is 12 BCM5482S PHYs.  */
            if ((ipd_port >= 16) && (ipd_port < 28))
                return ipd_port - 16;
	    return -1;
	case CVMX_BOARD_TYPE_CUST_LANNER_MR730:
            if ((ipd_port >= CVMX_HELPER_BOARD_MGMT_IPD_PORT) && (ipd_port < (CVMX_HELPER_BOARD_MGMT_IPD_PORT + 2)))
		return (ipd_port - CVMX_HELPER_BOARD_MGMT_IPD_PORT) + 0x81;
            if ((ipd_port >= 0) && (ipd_port < 4))
                return ipd_port;
	    return -1;
	case CVMX_BOARD_TYPE_CUST_LANNER_MR320:
	case CVMX_BOARD_TYPE_CUST_LANNER_MR321X:
	    /* Port 0 is a Marvell 88E6161 switch, ports 1 and 2 are Marvell
	       88E1111 interfaces.  */
	    switch (ipd_port) {
	    case 0:
		return 16;
	    case 1:
		return 1;
	    case 2:
		return 2;
	    default:
		return -1;
	    }
#endif
#if defined(OCTEON_VENDOR_UBIQUITI)
	case CVMX_BOARD_TYPE_CUST_UBIQUITI_E100:
	case CVMX_BOARD_TYPE_CUST_UBIQUITI_E120:
	    if (ipd_port > 2)
		return -1;
	    return (7 - ipd_port);
#endif
#if defined(OCTEON_VENDOR_RADISYS)
	case CVMX_BOARD_TYPE_CUST_RADISYS_RSYS4GBE:
	    /* No MII.  */
	    return -1;
#endif
#if defined(OCTEON_VENDOR_GEFES)
        case CVMX_BOARD_TYPE_AT5810:
		return -1;
        case CVMX_BOARD_TYPE_TNPA3804:
    	case CVMX_BOARD_TYPE_CUST_TNPA5804:
	case CVMX_BOARD_TYPE_CUST_W5800:
	case CVMX_BOARD_TYPE_WNPA3850:
	case CVMX_BOARD_TYPE_W3860:
		return -1;// RGMII boards should use inbad status
	case CVMX_BOARD_TYPE_CUST_W5651X:
	case CVMX_BOARD_TYPE_CUST_W5650:
	case CVMX_BOARD_TYPE_CUST_TNPA56X4:
	case CVMX_BOARD_TYPE_CUST_TNPA5651X:
        case CVMX_BOARD_TYPE_CUST_W63XX:
		return -1; /* No PHYs are connected to Octeon, PHYs inside of SFPs which is accessed over TWSI */
	case CVMX_BOARD_TYPE_CUST_W5434:
		/* Board has 4 SGMII ports. 4 connect out 
		 * must return the MII address of the PHY connected to each IPD port 
		 */
		if ((ipd_port >= 16) && (ipd_port < 20))
			return ipd_port - 16 + 0x40;
		else
			return -1;
#endif
    }

    /* Some unknown board. Somebody forgot to update this function... */
    cvmx_dprintf("%s: Unknown board type %d\n",
                 __FUNCTION__, cvmx_sysinfo_get()->board_type);
    return -1;
}
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(cvmx_helper_board_get_mii_address);
#endif

/**
 * @INTERNAL
 * Get link state of marvell PHY
 */
static cvmx_helper_link_info_t __get_marvell_phy_link_state(int phy_addr)
{
    cvmx_helper_link_info_t  result;
    int phy_status;

    result.u64 = 0;
    /*All the speed information can be read from register 17 in one go.*/
    phy_status = cvmx_mdio_read(phy_addr >> 8, phy_addr & 0xff, 17);

    /* If the resolve bit 11 isn't set, see if autoneg is turned off
       (bit 12, reg 0). The resolve bit doesn't get set properly when
       autoneg is off, so force it */
    if ((phy_status & (1<<11)) == 0)
    {
        int auto_status = cvmx_mdio_read(phy_addr >> 8, phy_addr & 0xff, 0);
        if ((auto_status & (1<<12)) == 0)
            phy_status |= 1<<11;
    }

    /* Only return a link if the PHY has finished auto negotiation
       and set the resolved bit (bit 11) */
    if (phy_status & (1<<11))
    {
        result.s.link_up = 1;
        result.s.full_duplex = ((phy_status>>13)&1);
        switch ((phy_status>>14)&3)
        {
            case 0: /* 10 Mbps */
                result.s.speed = 10;
                break;
            case 1: /* 100 Mbps */
                result.s.speed = 100;
                break;
            case 2: /* 1 Gbps */
                result.s.speed = 1000;
                break;
            case 3: /* Illegal */
                result.u64 = 0;
                break;
        }
    }
    return result;
}

/**
 * @INTERNAL
 * Get link state of broadcom PHY
 */
static cvmx_helper_link_info_t __get_broadcom_phy_link_state(int phy_addr)
{
    cvmx_helper_link_info_t  result;
    int phy_status;

    result.u64 = 0;
    /* Below we are going to read SMI/MDIO register 0x19 which works
       on Broadcom parts */
    phy_status = cvmx_mdio_read(phy_addr >> 8, phy_addr & 0xff, 0x19);
    switch ((phy_status>>8) & 0x7)
    {
        case 0:
            result.u64 = 0;
            break;
        case 1:
            result.s.link_up = 1;
            result.s.full_duplex = 0;
            result.s.speed = 10;
            break;
        case 2:
            result.s.link_up = 1;
            result.s.full_duplex = 1;
            result.s.speed = 10;
            break;
        case 3:
            result.s.link_up = 1;
            result.s.full_duplex = 0;
            result.s.speed = 100;
            break;
        case 4:
            result.s.link_up = 1;
            result.s.full_duplex = 1;
            result.s.speed = 100;
            break;
        case 5:
            result.s.link_up = 1;
            result.s.full_duplex = 1;
            result.s.speed = 100;
            break;
        case 6:
            result.s.link_up = 1;
            result.s.full_duplex = 0;
            result.s.speed = 1000;
            break;
        case 7:
            result.s.link_up = 1;
            result.s.full_duplex = 1;
            result.s.speed = 1000;
            break;
    }
    return result;
}


/**
 * @INTERNAL
 * Get link state using inband status
 */
static cvmx_helper_link_info_t __get_inband_link_state(int ipd_port)
{
    cvmx_helper_link_info_t  result;
    cvmx_gmxx_rxx_rx_inbnd_t inband_status;
    int interface = cvmx_helper_get_interface_num(ipd_port);
    int index = cvmx_helper_get_interface_index_num(ipd_port);

    result.u64 = 0;
    inband_status.u64 = cvmx_read_csr(CVMX_GMXX_RXX_RX_INBND(index, interface));
    result.s.link_up = inband_status.s.status;
    result.s.full_duplex = inband_status.s.duplex;
    switch (inband_status.s.speed)
    {
        case 0: /* 10 Mbps */
            result.s.speed = 10;
            break;
        case 1: /* 100 Mbps */
            result.s.speed = 100;
            break;
        case 2: /* 1 Gbps */
            result.s.speed = 1000;
            break;
        case 3: /* Illegal */
            result.u64 = 0;
            break;
    }
    return result;
}

#if !defined(CVMX_BUILD_FOR_LINUX_KERNEL) && (!defined(__FreeBSD__) || !defined(_KERNEL))
/**
 * @INTERNAL
 * Switch MDIO mux to the specified port.
 */
static int __switch_mdio_mux(int ipd_port)
{
    /* This method is board specific and doesn't use the device tree
       information as SE doesn't implement MDIO MUX abstration */
    switch (cvmx_sysinfo_get()->board_type)
    {
        case CVMX_BOARD_TYPE_EBB5600:
        {
            static unsigned char qlm_switch_addr = 0;
            /* Board has 1 management port */
            if (ipd_port == CVMX_HELPER_BOARD_MGMT_IPD_PORT)
                return 0;
            /* Board has 8 SGMII ports. 4 connected QLM1, 4 connected QLM3 */
            if ((ipd_port >= 0) && (ipd_port < 4))
            {
                if (qlm_switch_addr != 0x3)
                {
                    qlm_switch_addr = 0x3;  /* QLM1 */
                    cvmx_twsix_write_ia(0, 0x71, 0, 1, 1, qlm_switch_addr);
                    cvmx_wait_usec(11000); /* Let the write complete */
                }
                return ipd_port+1 + (1<<8);
            }
            else if ((ipd_port >= 16) && (ipd_port < 20))
            {
                if (qlm_switch_addr != 0xC)
                {
                    qlm_switch_addr = 0xC;  /* QLM3 */
                    cvmx_twsix_write_ia(0, 0x71, 0, 1, 1, qlm_switch_addr);
                    cvmx_wait_usec(11000); /* Let the write complete */
                }
                return ipd_port-16+1 + (1<<8);
            }
            else
                return -1;
        }
        case CVMX_BOARD_TYPE_EBB6600:
        {
            static unsigned char qlm_switch_addr = 0;
            int old_twsi_switch_reg;
            /* Board has 2 management ports */
            if ((ipd_port >= CVMX_HELPER_BOARD_MGMT_IPD_PORT) &&
                (ipd_port < (CVMX_HELPER_BOARD_MGMT_IPD_PORT + 2)))
                return ipd_port - CVMX_HELPER_BOARD_MGMT_IPD_PORT + 4;
            if ((ipd_port >= 0) && (ipd_port < 4)) /* QLM 2 */
            {
                if (qlm_switch_addr != 2)
                {
                    int tries;
                    qlm_switch_addr = 2;
                    tries = 3;
                    do {
                        old_twsi_switch_reg = cvmx_twsix_read8(0, 0x70, 0);
                    } while (tries-- > 0 && old_twsi_switch_reg < 0);
                    /* Set I2C MUX to enable port expander */
                    cvmx_retry_i2c_write(0, 0x70, 0, 1, 0, 8);
                    /* Set selecter to QLM 1 */
                    cvmx_retry_i2c_write(0, 0x38, 0, 1, 0, 0xff);
                    /* disable port expander */
                    cvmx_retry_i2c_write(0, 0x70, 0, 1, 0, old_twsi_switch_reg);
                }
                return 0x101 + ipd_port;
            }
            else if ((ipd_port >= 16) && (ipd_port < 20)) /* QLM 1 */
            {
                if (qlm_switch_addr != 1)
                {
                    int tries;
                    qlm_switch_addr = 1;
                    tries = 3;
                    do {
                            old_twsi_switch_reg = cvmx_twsix_read8(0, 0x70, 0);
                    } while (tries-- > 0 && old_twsi_switch_reg < 0);
                    /* Set I2C MUX to enable port expander */
                    cvmx_retry_i2c_write(0, 0x70, 0, 1, 0, 8);
                    /* Set selecter to QLM 2 */
                    cvmx_retry_i2c_write(0, 0x38, 0, 1, 0, 0xf7);
                    /* disable port expander */
                    cvmx_retry_i2c_write(0, 0x70, 0, 1, 0, old_twsi_switch_reg);
                }
                return 0x101 + (ipd_port - 16);
            } else
                return -1;
        }
        case CVMX_BOARD_TYPE_EBB6100:
        {
            static char gpio_configured = 0;

            if (!gpio_configured)
            {
                cvmx_gpio_cfg(3, 1);
                gpio_configured = 1;
            }
            /* Board has 2 management ports */
            if ((ipd_port >= CVMX_HELPER_BOARD_MGMT_IPD_PORT) &&
                (ipd_port < (CVMX_HELPER_BOARD_MGMT_IPD_PORT + 2)))
                return ipd_port - CVMX_HELPER_BOARD_MGMT_IPD_PORT + 4;
            if ((ipd_port >= 0) && (ipd_port < 4)) /* QLM 2 */
            {
                cvmx_gpio_set(1ull << 3);
                return 0x101 + ipd_port;
            }
            else if ((ipd_port >= 16) && (ipd_port < 20)) /* QLM 0 */
            {
                cvmx_gpio_clear(1ull << 3);
                return 0x101 + (ipd_port - 16);
            }
            else
            {
                printf("%s: Unknown ipd port 0x%x\n", __func__, ipd_port);
                return -1;
            }
        }
        default:
        {
            cvmx_dprintf("ERROR : unexpected mdio switch for board=%08x\n",
                         cvmx_sysinfo_get()->board_type);
            return -1;
        }
    }
    /* should never get here */
    return -1;
}

/**
 * @INTERNAL
 * This function is used ethernet ports link speed. This functions uses the
 * device tree information to determine the phy address and type of PHY.
 * The only supproted PHYs are Marvell and Broadcom.
 *
 * @param ipd_port IPD input port associated with the port we want to get link
 *                 status for.
 *
 * @return The ports link status. If the link isn't fully resolved, this must
 *         return zero.
 */

cvmx_helper_link_info_t __cvmx_helper_board_link_get_from_dt(int ipd_port)
{
    cvmx_helper_link_info_t  result;
    cvmx_phy_info_t phy_info;

    result.u64 = 0;
    if (cvmx_sysinfo_get()->board_type == CVMX_BOARD_TYPE_SIM)
    {
        /* The simulator gives you a simulated 1Gbps full duplex link */
        result.s.link_up = 1;
        result.s.full_duplex = 1;
        result.s.speed = 1000;
        return result;
    }
    phy_info = __get_phy_info_from_dt(ipd_port);
    //cvmx_dprintf("ipd_port=%d phy_addr=%d dc=%d type=%d \n", ipd_port,
    //             phy_info.phy_addr, phy_info.direct_connect, phy_info.phy_type);
    if (phy_info.phy_addr < 0) return result;

    if (phy_info.direct_connect == 0)
        __switch_mdio_mux(ipd_port);
    switch(phy_info.phy_type)
    {
        case BROADCOM_GENERIC_PHY:
            result = __get_broadcom_phy_link_state(phy_info.phy_addr);
            break;
        case MARVELL_GENERIC_PHY:
            result = __get_marvell_phy_link_state(phy_info.phy_addr);
            break;
        default:
            result = __get_inband_link_state(ipd_port);
    }
    return result;

}
#endif

/**
 * @INTERNAL
 * This function invokes  __cvmx_helper_board_link_get_from_dt when device tree
 * info is available. When the device tree information is not available then
 * this function is the board specific method of determining an
 * ethernet ports link speed. Most Octeon boards have Marvell PHYs
 * and are handled by the fall through case. This function must be
 * updated for boards that don't have the normal Marvell PHYs.
 *
 * This function must be modified for every new Octeon board.
 * Internally it uses switch statements based on the cvmx_sysinfo
 * data to determine board types and revisions. It relies on the
 * fact that every Octeon board receives a unique board type
 * enumeration from the bootloader.
 *
 * @param ipd_port IPD input port associated with the port we want to get link
 *                 status for.
 *
 * @return The ports link status. If the link isn't fully resolved, this must
 *         return zero.
 */
cvmx_helper_link_info_t __cvmx_helper_board_link_get(int ipd_port)
{
    cvmx_helper_link_info_t result;
    int phy_addr;
    int is_broadcom_phy = 0;

#if !defined(CVMX_BUILD_FOR_LINUX_KERNEL) && (!defined(__FreeBSD__) || !defined(_KERNEL))
    if (cvmx_sysinfo_get()->fdt_addr)
    {
        return __cvmx_helper_board_link_get_from_dt(ipd_port);
    }
#endif

    /* Give the user a chance to override the processing of this function */
    if (cvmx_override_board_link_get)
        return cvmx_override_board_link_get(ipd_port);

    /* Unless we fix it later, all links are defaulted to down */
    result.u64 = 0;

#if !defined(OCTEON_BOARD_CAPK_0100ND)
    /* This switch statement should handle all ports that either don't use
        Marvell PHYS, or don't support in-band status */
    switch (cvmx_sysinfo_get()->board_type)
    {
        case CVMX_BOARD_TYPE_SIM:
            /* The simulator gives you a simulated 1Gbps full duplex link */
            result.s.link_up = 1;
            result.s.full_duplex = 1;
            result.s.speed = 1000;
            return result;
        case CVMX_BOARD_TYPE_LANAI2_A:
        case CVMX_BOARD_TYPE_LANAI2_U:
        case CVMX_BOARD_TYPE_LANAI2_G:
            break;
        case CVMX_BOARD_TYPE_EBH3100:
        case CVMX_BOARD_TYPE_CN3010_EVB_HS5:
        case CVMX_BOARD_TYPE_CN3005_EVB_HS5:
#if !defined(OCTEON_VENDOR_GEFES)
        case CVMX_BOARD_TYPE_CN3020_EVB_HS5:
#endif
            /* Port 1 on these boards is always Gigabit */
            if (ipd_port == 1)
            {
                result.s.link_up = 1;
                result.s.full_duplex = 1;
                result.s.speed = 1000;
                return result;
            }
            /* Fall through to the generic code below */
            break;
        case CVMX_BOARD_TYPE_EBT5600:
        case CVMX_BOARD_TYPE_EBH5600:
        case CVMX_BOARD_TYPE_EBH5601:
        case CVMX_BOARD_TYPE_EBH5610:
            /* Board has 1 management ports */
            if (ipd_port == CVMX_HELPER_BOARD_MGMT_IPD_PORT)
                is_broadcom_phy = 1;
            break;
        case CVMX_BOARD_TYPE_EBH5200:
        case CVMX_BOARD_TYPE_EBH5201:
        case CVMX_BOARD_TYPE_EBT5200:
            /* Board has 2 management ports */
            if ((ipd_port >= CVMX_HELPER_BOARD_MGMT_IPD_PORT) && (ipd_port < (CVMX_HELPER_BOARD_MGMT_IPD_PORT + 2)))
                is_broadcom_phy = 1;
            break;
        case CVMX_BOARD_TYPE_EBB6100:
        case CVMX_BOARD_TYPE_EBB6300:   /* Only for MII mode, with PHY addresses 0/1. Default is RGMII*/
        case CVMX_BOARD_TYPE_EBB6600:   /* Only for MII mode, with PHY addresses 0/1. Default is RGMII*/
            if ((ipd_port >= CVMX_HELPER_BOARD_MGMT_IPD_PORT) && (ipd_port < (CVMX_HELPER_BOARD_MGMT_IPD_PORT + 2))
                && cvmx_helper_board_get_mii_address(ipd_port) >= 0 && cvmx_helper_board_get_mii_address(ipd_port) <= 1)
                is_broadcom_phy = 1;
            break;
        case CVMX_BOARD_TYPE_EP6300C:
            is_broadcom_phy = 1;
            break;
        case CVMX_BOARD_TYPE_CUST_NB5:
            /* Port 1 on these boards is always Gigabit */
            if (ipd_port == 1)
            {
                result.s.link_up = 1;
                result.s.full_duplex = 1;
                result.s.speed = 1000;
                return result;
            }
            else /* The other port uses a broadcom PHY */
                is_broadcom_phy = 1;
            break;
        case CVMX_BOARD_TYPE_BBGW_REF:
            /* Port 1 on these boards is always Gigabit */
            if (ipd_port == 2)
            {
                /* Port 2 is not hooked up */
                result.u64 = 0;
                return result;
            }
            else
            {
                /* Ports 0 and 1 connect to the switch */
                result.s.link_up = 1;
                result.s.full_duplex = 1;
                result.s.speed = 1000;
                return result;
            }
        case CVMX_BOARD_TYPE_NIC4E:
        case CVMX_BOARD_TYPE_NIC2E:
            is_broadcom_phy = 1;
            break;
	/* Private vendor-defined boards.  */
#if defined(OCTEON_VENDOR_LANNER)
	case CVMX_BOARD_TYPE_CUST_LANNER_MR730:
	    /* Ports are BCM5482S */
	    is_broadcom_phy = 1;
	    break;
	case CVMX_BOARD_TYPE_CUST_LANNER_MR320:
	case CVMX_BOARD_TYPE_CUST_LANNER_MR321X:
	    /* Port 0 connects to the switch */
	    if (ipd_port == 0)
	    {
                result.s.link_up = 1;
                result.s.full_duplex = 1;
                result.s.speed = 1000;
		return result;
	    }
	    break;
#endif
#if defined(OCTEON_VENDOR_GEFES)
	case CVMX_BOARD_TYPE_CUST_TNPA5651X:
	   /* Since we don't auto-negotiate... 1Gbps full duplex link */
	   result.s.link_up = 1;
	   result.s.full_duplex = 1;
	   result.s.speed = 1000;
	   return result;
	   break;
#endif
    }
#endif

    phy_addr = cvmx_helper_board_get_mii_address(ipd_port);
    //cvmx_dprintf("ipd_port=%d phy_addr=%d broadcom=%d\n",
    //             ipd_port, phy_addr, is_broadcom_phy);
    if (phy_addr != -1)
    {
        if (is_broadcom_phy)
        {
            result =  __get_broadcom_phy_link_state(phy_addr);
        }
        else
        {
            /* This code assumes we are using a Marvell Gigabit PHY. */
            result = __get_marvell_phy_link_state(phy_addr);
        }
    }
    else if (OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN58XX)
             || OCTEON_IS_MODEL(OCTEON_CN50XX))
    {
        /* We don't have a PHY address, so attempt to use in-band status. It is
            really important that boards not supporting in-band status never get
            here. Reading broken in-band status tends to do bad things */
        result = __get_inband_link_state(ipd_port);
    }
#if defined(OCTEON_VENDOR_GEFES)
    else if( (OCTEON_IS_MODEL(OCTEON_CN56XX)) || (OCTEON_IS_MODEL(OCTEON_CN63XX)) ) 
    {
        int interface = cvmx_helper_get_interface_num(ipd_port);
        int index = cvmx_helper_get_interface_index_num(ipd_port);
        cvmx_pcsx_miscx_ctl_reg_t mode_type;
        cvmx_pcsx_mrx_status_reg_t mrx_status;
        cvmx_pcsx_anx_adv_reg_t anxx_adv;
        cvmx_pcsx_sgmx_lp_adv_reg_t sgmii_inband_status;

        anxx_adv.u64 = cvmx_read_csr(CVMX_PCSX_ANX_ADV_REG(index, interface));
        mrx_status.u64 = cvmx_read_csr(CVMX_PCSX_MRX_STATUS_REG(index, interface));

        mode_type.u64 = cvmx_read_csr(CVMX_PCSX_MISCX_CTL_REG(index, interface));

        /* Read Octeon's inband status */
        sgmii_inband_status.u64 = cvmx_read_csr(CVMX_PCSX_SGMX_LP_ADV_REG(index, interface));

        result.s.link_up = sgmii_inband_status.s.link; 
        result.s.full_duplex = sgmii_inband_status.s.dup;
        switch (sgmii_inband_status.s.speed)
        {
        case 0: /* 10 Mbps */
            result.s.speed = 10;
            break;
        case 1: /* 100 Mbps */
            result.s.speed = 100;
            break;
        case 2: /* 1 Gbps */
            result.s.speed = 1000;
            break;
        case 3: /* Illegal */
            result.s.speed = 0;
            result.s.link_up = 0;
            break;
        }
    }
#endif
    else
    {
        /* We don't have a PHY address and we don't have in-band status. There
            is no way to determine the link speed. Return down assuming this
            port isn't wired */
        result.u64 = 0;
    }

    /* If link is down, return all fields as zero. */
    if (!result.s.link_up)
        result.u64 = 0;

    return result;
}


/**
 * This function as a board specific method of changing the PHY
 * speed, duplex, and autonegotiation. This programs the PHY and
 * not Octeon. This can be used to force Octeon's links to
 * specific settings.
 *
 * @param phy_addr  The address of the PHY to program
 * @param link_flags
 *                  Flags to control autonegotiation.  Bit 0 is autonegotiation
 *                  enable/disable to maintain backward compatibility.
 * @param link_info Link speed to program. If the speed is zero and autonegotiation
 *                  is enabled, all possible negotiation speeds are advertised.
 *
 * @return Zero on success, negative on failure
 */
int cvmx_helper_board_link_set_phy(int phy_addr, cvmx_helper_board_set_phy_link_flags_types_t link_flags,
                                   cvmx_helper_link_info_t link_info)
{

    /* Set the flow control settings based on link_flags */
    if ((link_flags & set_phy_link_flags_flow_control_mask) != set_phy_link_flags_flow_control_dont_touch)
    {
        cvmx_mdio_phy_reg_autoneg_adver_t reg_autoneg_adver;
        reg_autoneg_adver.u16 = cvmx_mdio_read(phy_addr >> 8, phy_addr & 0xff, CVMX_MDIO_PHY_REG_AUTONEG_ADVER);
        reg_autoneg_adver.s.asymmetric_pause = (link_flags & set_phy_link_flags_flow_control_mask) == set_phy_link_flags_flow_control_enable;
        reg_autoneg_adver.s.pause = (link_flags & set_phy_link_flags_flow_control_mask) == set_phy_link_flags_flow_control_enable;
        cvmx_mdio_write(phy_addr >> 8, phy_addr & 0xff, CVMX_MDIO_PHY_REG_AUTONEG_ADVER, reg_autoneg_adver.u16);
    }

    /* If speed isn't set and autoneg is on advertise all supported modes */
    if ((link_flags & set_phy_link_flags_autoneg) && (link_info.s.speed == 0))
    {
        cvmx_mdio_phy_reg_control_t reg_control;
        cvmx_mdio_phy_reg_status_t reg_status;
        cvmx_mdio_phy_reg_autoneg_adver_t reg_autoneg_adver;
        cvmx_mdio_phy_reg_extended_status_t reg_extended_status;
        cvmx_mdio_phy_reg_control_1000_t reg_control_1000;

        reg_status.u16 = cvmx_mdio_read(phy_addr >> 8, phy_addr & 0xff, CVMX_MDIO_PHY_REG_STATUS);
        reg_autoneg_adver.u16 = cvmx_mdio_read(phy_addr >> 8, phy_addr & 0xff, CVMX_MDIO_PHY_REG_AUTONEG_ADVER);
        reg_autoneg_adver.s.advert_100base_t4 = reg_status.s.capable_100base_t4;
        reg_autoneg_adver.s.advert_10base_tx_full = reg_status.s.capable_10_full;
        reg_autoneg_adver.s.advert_10base_tx_half = reg_status.s.capable_10_half;
        reg_autoneg_adver.s.advert_100base_tx_full = reg_status.s.capable_100base_x_full;
        reg_autoneg_adver.s.advert_100base_tx_half = reg_status.s.capable_100base_x_half;
        cvmx_mdio_write(phy_addr >> 8, phy_addr & 0xff, CVMX_MDIO_PHY_REG_AUTONEG_ADVER, reg_autoneg_adver.u16);
        if (reg_status.s.capable_extended_status)
        {
            reg_extended_status.u16 = cvmx_mdio_read(phy_addr >> 8, phy_addr & 0xff, CVMX_MDIO_PHY_REG_EXTENDED_STATUS);
            reg_control_1000.u16 = cvmx_mdio_read(phy_addr >> 8, phy_addr & 0xff, CVMX_MDIO_PHY_REG_CONTROL_1000);
            reg_control_1000.s.advert_1000base_t_full = reg_extended_status.s.capable_1000base_t_full;
            reg_control_1000.s.advert_1000base_t_half = reg_extended_status.s.capable_1000base_t_half;
            cvmx_mdio_write(phy_addr >> 8, phy_addr & 0xff, CVMX_MDIO_PHY_REG_CONTROL_1000, reg_control_1000.u16);
        }
        reg_control.u16 = cvmx_mdio_read(phy_addr >> 8, phy_addr & 0xff, CVMX_MDIO_PHY_REG_CONTROL);
        reg_control.s.autoneg_enable = 1;
        reg_control.s.restart_autoneg = 1;
        cvmx_mdio_write(phy_addr >> 8, phy_addr & 0xff, CVMX_MDIO_PHY_REG_CONTROL, reg_control.u16);
    }
    else if ((link_flags & set_phy_link_flags_autoneg))
    {
        cvmx_mdio_phy_reg_control_t reg_control;
        cvmx_mdio_phy_reg_status_t reg_status;
        cvmx_mdio_phy_reg_autoneg_adver_t reg_autoneg_adver;
        cvmx_mdio_phy_reg_control_1000_t reg_control_1000;

        reg_status.u16 = cvmx_mdio_read(phy_addr >> 8, phy_addr & 0xff, CVMX_MDIO_PHY_REG_STATUS);
        reg_autoneg_adver.u16 = cvmx_mdio_read(phy_addr >> 8, phy_addr & 0xff, CVMX_MDIO_PHY_REG_AUTONEG_ADVER);
        reg_autoneg_adver.s.advert_100base_t4 = 0;
        reg_autoneg_adver.s.advert_10base_tx_full = 0;
        reg_autoneg_adver.s.advert_10base_tx_half = 0;
        reg_autoneg_adver.s.advert_100base_tx_full = 0;
        reg_autoneg_adver.s.advert_100base_tx_half = 0;
        if (reg_status.s.capable_extended_status)
        {
            reg_control_1000.u16 = cvmx_mdio_read(phy_addr >> 8, phy_addr & 0xff, CVMX_MDIO_PHY_REG_CONTROL_1000);
            reg_control_1000.s.advert_1000base_t_full = 0;
            reg_control_1000.s.advert_1000base_t_half = 0;
        }
        switch (link_info.s.speed)
        {
            case 10:
                reg_autoneg_adver.s.advert_10base_tx_full = link_info.s.full_duplex;
                reg_autoneg_adver.s.advert_10base_tx_half = !link_info.s.full_duplex;
                break;
            case 100:
                reg_autoneg_adver.s.advert_100base_tx_full = link_info.s.full_duplex;
                reg_autoneg_adver.s.advert_100base_tx_half = !link_info.s.full_duplex;
                break;
            case 1000:
                reg_control_1000.s.advert_1000base_t_full = link_info.s.full_duplex;
                reg_control_1000.s.advert_1000base_t_half = !link_info.s.full_duplex;
                break;
        }
        cvmx_mdio_write(phy_addr >> 8, phy_addr & 0xff, CVMX_MDIO_PHY_REG_AUTONEG_ADVER, reg_autoneg_adver.u16);
        if (reg_status.s.capable_extended_status)
            cvmx_mdio_write(phy_addr >> 8, phy_addr & 0xff, CVMX_MDIO_PHY_REG_CONTROL_1000, reg_control_1000.u16);
        reg_control.u16 = cvmx_mdio_read(phy_addr >> 8, phy_addr & 0xff, CVMX_MDIO_PHY_REG_CONTROL);
        reg_control.s.autoneg_enable = 1;
        reg_control.s.restart_autoneg = 1;
        cvmx_mdio_write(phy_addr >> 8, phy_addr & 0xff, CVMX_MDIO_PHY_REG_CONTROL, reg_control.u16);
    }
    else
    {
        cvmx_mdio_phy_reg_control_t reg_control;
        reg_control.u16 = cvmx_mdio_read(phy_addr >> 8, phy_addr & 0xff, CVMX_MDIO_PHY_REG_CONTROL);
        reg_control.s.autoneg_enable = 0;
        reg_control.s.restart_autoneg = 1;
        reg_control.s.duplex = link_info.s.full_duplex;
        if (link_info.s.speed == 1000)
        {
            reg_control.s.speed_msb = 1;
            reg_control.s.speed_lsb = 0;
        }
        else if (link_info.s.speed == 100)
        {
            reg_control.s.speed_msb = 0;
            reg_control.s.speed_lsb = 1;
        }
        else if (link_info.s.speed == 10)
        {
            reg_control.s.speed_msb = 0;
            reg_control.s.speed_lsb = 0;
        }
        cvmx_mdio_write(phy_addr >> 8, phy_addr & 0xff, CVMX_MDIO_PHY_REG_CONTROL, reg_control.u16);
    }
    return 0;
}


/**
 * @INTERNAL
 * This function is called by cvmx_helper_interface_probe() after it
 * determines the number of ports Octeon can support on a specific
 * interface. This function is the per board location to override
 * this value. It is called with the number of ports Octeon might
 * support and should return the number of actual ports on the
 * board.
 *
 * This function must be modified for every new Octeon board.
 * Internally it uses switch statements based on the cvmx_sysinfo
 * data to determine board types and revisions. It relies on the
 * fact that every Octeon board receives a unique board type
 * enumeration from the bootloader.
 *
 * @param interface Interface to probe
 * @param supported_ports
 *                  Number of ports Octeon supports.
 *
 * @return Number of ports the actual board supports. Many times this will
 *         simple be "support_ports".
 */
int __cvmx_helper_board_interface_probe(int interface, int supported_ports)
{
    switch (cvmx_sysinfo_get()->board_type)
    {
        case CVMX_BOARD_TYPE_CN3005_EVB_HS5:
        case CVMX_BOARD_TYPE_LANAI2_A:
        case CVMX_BOARD_TYPE_LANAI2_U:
        case CVMX_BOARD_TYPE_LANAI2_G:
            if (interface == 0)
                return 2;
	    break;
        case CVMX_BOARD_TYPE_BBGW_REF:
            if (interface == 0)
                return 2;
	    break;
        case CVMX_BOARD_TYPE_NIC_XLE_4G:
            if (interface == 0)
                return 0;
	    break;
        /* The 2nd interface on the EBH5600 is connected to the Marvel switch,
            which we don't support. Disable ports connected to it */
        case CVMX_BOARD_TYPE_EBH5600:
            if (interface == 1)
                return 0;
	    break;
        case CVMX_BOARD_TYPE_EBB5600:
#ifdef CVMX_ENABLE_PKO_FUNCTIONS
            if (cvmx_helper_interface_get_mode(interface) == CVMX_HELPER_INTERFACE_MODE_PICMG)
                return 0;
#endif
	    break;
        case CVMX_BOARD_TYPE_EBT5600:
	    /* Disable loopback.  */
	    if (interface == 3)
		return 0;
	    break;
        case CVMX_BOARD_TYPE_EBT5810:
            return 1;  /* Two ports on each SPI: 1 hooked to MAC, 1 loopback
                       ** Loopback disabled by default. */
        case CVMX_BOARD_TYPE_NIC2E:
            if (interface == 0)
                return 2;
#if defined(OCTEON_VENDOR_LANNER)
	case CVMX_BOARD_TYPE_CUST_LANNER_MR955:
	    if (interface == 1)
	        return 12;
	    break;
#endif
#if defined(OCTEON_VENDOR_GEFES)
        case CVMX_BOARD_TYPE_CUST_TNPA5651X:
                if (interface < 2) /* interface can be EITHER 0 or 1 */
			return 1;//always return 1 for XAUI and SGMII mode. 
		break;
        case CVMX_BOARD_TYPE_CUST_TNPA56X4:
		if ((interface == 0) && 
			(cvmx_helper_interface_get_mode(interface) == CVMX_HELPER_INTERFACE_MODE_SGMII))
		{
			cvmx_pcsx_miscx_ctl_reg_t pcsx_miscx_ctl_reg;
	
			/* For this port we need to set the mode to 1000BaseX */
			pcsx_miscx_ctl_reg.u64 =
				cvmx_read_csr(CVMX_PCSX_MISCX_CTL_REG(0, interface));
			pcsx_miscx_ctl_reg.cn56xx.mode = 1;
			cvmx_write_csr(CVMX_PCSX_MISCX_CTL_REG(0, interface),
						   pcsx_miscx_ctl_reg.u64);
			pcsx_miscx_ctl_reg.u64 =
				cvmx_read_csr(CVMX_PCSX_MISCX_CTL_REG(1, interface));
			pcsx_miscx_ctl_reg.cn56xx.mode = 1;
			cvmx_write_csr(CVMX_PCSX_MISCX_CTL_REG(1, interface),
						   pcsx_miscx_ctl_reg.u64);
	
			return 2;        
		} 
		break;
#endif
    }
#ifdef CVMX_BUILD_FOR_UBOOT
    if (CVMX_HELPER_INTERFACE_MODE_SPI == cvmx_helper_interface_get_mode(interface) && getenv("disable_spi"))
        return 0;
#endif
    return supported_ports;
}


/**
 * @INTERNAL
 * Enable packet input/output from the hardware. This function is
 * called after by cvmx_helper_packet_hardware_enable() to
 * perform board specific initialization. For most boards
 * nothing is needed.
 *
 * @param interface Interface to enable
 *
 * @return Zero on success, negative on failure
 */
int __cvmx_helper_board_hardware_enable(int interface)
{
    if (cvmx_sysinfo_get()->board_type == CVMX_BOARD_TYPE_CN3005_EVB_HS5)
    {
        if (interface == 0)
        {
            /* Different config for switch port */
            cvmx_write_csr(CVMX_ASXX_TX_CLK_SETX(1, interface), 0);
            cvmx_write_csr(CVMX_ASXX_RX_CLK_SETX(1, interface), 0);
            /* Boards with gigabit WAN ports need a different setting that is
                compatible with 100 Mbit settings */
            cvmx_write_csr(CVMX_ASXX_TX_CLK_SETX(0, interface), 0xc);
            cvmx_write_csr(CVMX_ASXX_RX_CLK_SETX(0, interface), 0xc);
        }
    }
    else if (cvmx_sysinfo_get()->board_type == CVMX_BOARD_TYPE_LANAI2_U)
    {
        if (interface == 0)
        {
            cvmx_write_csr(CVMX_ASXX_TX_CLK_SETX(0, interface), 16);
            cvmx_write_csr(CVMX_ASXX_RX_CLK_SETX(0, interface), 16);
        }
    }
    else if (cvmx_sysinfo_get()->board_type == CVMX_BOARD_TYPE_CN3010_EVB_HS5)
    {
        /* Broadcom PHYs require different ASX clocks. Unfortunately
            many customer don't define a new board Id and simply
            mangle the CN3010_EVB_HS5 */
        if (interface == 0)
        {
            /* Some customers boards use a hacked up bootloader that identifies them as
            ** CN3010_EVB_HS5 evaluation boards.  This leads to all kinds of configuration
            ** problems.  Detect one case, and print warning, while trying to do the right thing.
            */
            int phy_addr = cvmx_helper_board_get_mii_address(0);
            if (phy_addr != -1)
            {
                int phy_identifier = cvmx_mdio_read(phy_addr >> 8, phy_addr & 0xff, 0x2);
                /* Is it a Broadcom PHY? */
                if (phy_identifier == 0x0143)
                {
                    cvmx_dprintf("\n");
                    cvmx_dprintf("ERROR:\n");
                    cvmx_dprintf("ERROR: Board type is CVMX_BOARD_TYPE_CN3010_EVB_HS5, but Broadcom PHY found.\n");
                    cvmx_dprintf("ERROR: The board type is mis-configured, and software malfunctions are likely.\n");
                    cvmx_dprintf("ERROR: All boards require a unique board type to identify them.\n");
                    cvmx_dprintf("ERROR:\n");
                    cvmx_dprintf("\n");
                    cvmx_wait(1000000000);
                    cvmx_write_csr(CVMX_ASXX_RX_CLK_SETX(0, interface), 5);
                    cvmx_write_csr(CVMX_ASXX_TX_CLK_SETX(0, interface), 5);
                }
            }
        }
    }
#if defined(OCTEON_VENDOR_UBIQUITI)
    else if (cvmx_sysinfo_get()->board_type == CVMX_BOARD_TYPE_CUST_UBIQUITI_E100 ||
        cvmx_sysinfo_get()->board_type == CVMX_BOARD_TYPE_CUST_UBIQUITI_E120)
    {
	/* Configure ASX cloks for all ports on interface 0.  */
	if (interface == 0)
	{
	    int port;

	    for (port = 0; port < 3; port++) {
                cvmx_write_csr(CVMX_ASXX_TX_CLK_SETX(port, interface), 16);
                cvmx_write_csr(CVMX_ASXX_RX_CLK_SETX(port, interface), 0);
	    }
	}
    }
#endif
    return 0;
}


/**
 * @INTERNAL
 * Gets the clock type used for the USB block based on board type.
 * Used by the USB code for auto configuration of clock type.
 *
 * @return USB clock type enumeration
 */
cvmx_helper_board_usb_clock_types_t __cvmx_helper_board_usb_get_clock_type(void)
{
#if !defined(CVMX_BUILD_FOR_LINUX_KERNEL) && (!defined(__FreeBSD__) || !defined(_KERNEL))
    const void *fdt_addr = CASTPTR(const void *, cvmx_sysinfo_get()->fdt_addr);
    int nodeoffset;
    const void *nodep;
    int len;
    uint32_t speed = 0;
    const char *type = NULL;

    if (fdt_addr)
    {
        nodeoffset = fdt_path_offset(fdt_addr, "/soc/uctl");
        if (nodeoffset < 0)
            nodeoffset = fdt_path_offset(fdt_addr, "/soc/usbn");

        if (nodeoffset >= 0)
        {
            nodep = fdt_getprop(fdt_addr, nodeoffset, "refclk-type", &len);
            if (nodep != NULL && len > 0)
                type = (const char *)nodep;
            else
                type = "unknown";
            nodep = fdt_getprop(fdt_addr, nodeoffset, "refclk-frequency", &len);
            if (nodep != NULL && len == sizeof(uint32_t))
                speed = fdt32_to_cpu(*(int *)nodep);
            else
                speed = 0;
            if (!strcmp(type, "crystal"))
            {
                if (speed == 0 || speed == 12000000)
                    return USB_CLOCK_TYPE_CRYSTAL_12;
                else
                    printf("Warning: invalid crystal speed for USB clock type in FDT\n");
            }
            else if (!strcmp(type, "external"))
            {
                switch (speed) {
                case 12000000:
                    return USB_CLOCK_TYPE_REF_12;
                case 24000000:
                    return USB_CLOCK_TYPE_REF_24;
                case 0:
                case 48000000:
                    return USB_CLOCK_TYPE_REF_48;
                default:
                    printf("Warning: invalid USB clock speed of %u hz in FDT\n", speed);
                }
            }
            else
                printf("Warning: invalid USB reference clock type \"%s\" in FDT\n", type ? type : "NULL");
        }
    }
#endif
    switch (cvmx_sysinfo_get()->board_type)
    {
        case CVMX_BOARD_TYPE_BBGW_REF:
        case CVMX_BOARD_TYPE_LANAI2_A:
        case CVMX_BOARD_TYPE_LANAI2_U:
        case CVMX_BOARD_TYPE_LANAI2_G:
#if defined(OCTEON_VENDOR_LANNER)
        case CVMX_BOARD_TYPE_CUST_LANNER_MR320:
        case CVMX_BOARD_TYPE_CUST_LANNER_MR321X:
#endif
#if defined(OCTEON_VENDOR_UBIQUITI)
        case CVMX_BOARD_TYPE_CUST_UBIQUITI_E100:
        case CVMX_BOARD_TYPE_CUST_UBIQUITI_E120:
#endif
#if defined(OCTEON_BOARD_CAPK_0100ND)
	case CVMX_BOARD_TYPE_CN3010_EVB_HS5:
#endif
#if defined(OCTEON_VENDOR_GEFES) /* All GEFES' boards use same xtal type */
        case CVMX_BOARD_TYPE_TNPA3804:
        case CVMX_BOARD_TYPE_AT5810:
        case CVMX_BOARD_TYPE_WNPA3850:
        case CVMX_BOARD_TYPE_W3860:
        case CVMX_BOARD_TYPE_CUST_TNPA5804:
        case CVMX_BOARD_TYPE_CUST_W5434:
        case CVMX_BOARD_TYPE_CUST_W5650:
        case CVMX_BOARD_TYPE_CUST_W5800:
        case CVMX_BOARD_TYPE_CUST_W5651X:
        case CVMX_BOARD_TYPE_CUST_TNPA5651X:
        case CVMX_BOARD_TYPE_CUST_TNPA56X4:
        case CVMX_BOARD_TYPE_CUST_W63XX:
#endif
        case CVMX_BOARD_TYPE_NIC10E_66:
            return USB_CLOCK_TYPE_CRYSTAL_12;
        case CVMX_BOARD_TYPE_NIC10E:
            return USB_CLOCK_TYPE_REF_12;
        default:
            break;
    }
    if (OCTEON_IS_MODEL(OCTEON_CN6XXX)	/* Most boards except NIC10e use a 12MHz crystal */
        || OCTEON_IS_MODEL(OCTEON_CNF7XXX))
        return USB_CLOCK_TYPE_CRYSTAL_12;
    return USB_CLOCK_TYPE_REF_48;
}


/**
 * @INTERNAL
 * Adjusts the number of available USB ports on Octeon based on board
 * specifics.
 *
 * @param supported_ports expected number of ports based on chip type;
 *
 *
 * @return number of available usb ports, based on board specifics.
 *         Return value is supported_ports if function does not
 *         override.
 */
int __cvmx_helper_board_usb_get_num_ports(int supported_ports)
{
    switch (cvmx_sysinfo_get()->board_type)
    {
        case CVMX_BOARD_TYPE_NIC_XLE_4G:
        case CVMX_BOARD_TYPE_NIC2E:
            return 0;
    }

    return supported_ports;
}


