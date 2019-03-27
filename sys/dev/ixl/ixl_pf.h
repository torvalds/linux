/******************************************************************************

  Copyright (c) 2013-2018, Intel Corporation
  All rights reserved.
  
  Redistribution and use in source and binary forms, with or without 
  modification, are permitted provided that the following conditions are met:
  
   1. Redistributions of source code must retain the above copyright notice, 
      this list of conditions and the following disclaimer.
  
   2. Redistributions in binary form must reproduce the above copyright 
      notice, this list of conditions and the following disclaimer in the 
      documentation and/or other materials provided with the distribution.
  
   3. Neither the name of the Intel Corporation nor the names of its 
      contributors may be used to endorse or promote products derived from 
      this software without specific prior written permission.
  
  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE 
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.

******************************************************************************/
/*$FreeBSD$*/


#ifndef _IXL_PF_H_
#define _IXL_PF_H_

#include "ixl.h"
#include "ixl_pf_qmgr.h"

#define	VF_FLAG_ENABLED			0x01
#define	VF_FLAG_SET_MAC_CAP		0x02
#define	VF_FLAG_VLAN_CAP		0x04
#define	VF_FLAG_PROMISC_CAP		0x08
#define	VF_FLAG_MAC_ANTI_SPOOF		0x10

#define IXL_ICR0_CRIT_ERR_MASK 			\
    (I40E_PFINT_ICR0_PCI_EXCEPTION_MASK | 	\
     I40E_PFINT_ICR0_ECC_ERR_MASK | 		\
     I40E_PFINT_ICR0_PE_CRITERR_MASK)

/* VF Interrupts */
#define IXL_VPINT_LNKLSTN_REG(hw, vector, vf_num) \
	I40E_VPINT_LNKLSTN(((vector) - 1) + \
	    (((hw)->func_caps.num_msix_vectors_vf - 1) * (vf_num)))

#define IXL_VFINT_DYN_CTLN_REG(hw, vector, vf_num) \
	I40E_VFINT_DYN_CTLN(((vector) - 1) + \
	    (((hw)->func_caps.num_msix_vectors_vf - 1) * (vf_num)))

/* Used in struct ixl_pf's state field */
enum ixl_pf_state {
	IXL_PF_STATE_ADAPTER_RESETTING	= (1 << 0),
	IXL_PF_STATE_MDD_PENDING	= (1 << 1),
	IXL_PF_STATE_PF_RESET_REQ	= (1 << 2),
	IXL_PF_STATE_VF_RESET_REQ	= (1 << 3),
	IXL_PF_STATE_PF_CRIT_ERR	= (1 << 4),
	IXL_PF_STATE_CORE_RESET_REQ	= (1 << 5),
	IXL_PF_STATE_GLOB_RESET_REQ	= (1 << 6),
	IXL_PF_STATE_EMP_RESET_REQ	= (1 << 7),
	IXL_PF_STATE_FW_LLDP_DISABLED	= (1 << 8),
};

struct ixl_vf {
	struct ixl_vsi		vsi;
	u32			vf_flags;
	u32			num_mdd_events;

	u8			mac[ETHER_ADDR_LEN];
	u16			vf_num;
	u32			version;

	struct ixl_pf_qtag	qtag;
	struct sysctl_ctx_list	ctx;
};

/* Physical controller structure */
struct ixl_pf {
	struct ixl_vsi		vsi;

	struct i40e_hw		hw;
	struct i40e_osdep	osdep;
	device_t		dev;

	struct resource		*pci_mem;

#ifdef IXL_IW
	int			iw_msix;
	bool			iw_enabled;
#endif
	u32			state;
	u8			supported_speeds;

	struct ixl_pf_qmgr	qmgr;
	struct ixl_pf_qtag	qtag;

	/* Tunable values */
	bool			enable_tx_fc_filter;
	int			dynamic_rx_itr;
	int			dynamic_tx_itr;
	int			tx_itr;
	int			rx_itr;
	int			enable_vf_loopback;

	bool			link_up;
	int			advertised_speed;
	int			fc; /* link flow ctrl setting */
	enum ixl_dbg_mask	dbg_mask;
	bool			has_i2c;

	/* Misc stats maintained by the driver */
	u64			admin_irq;

	/* Statistics from hw */
	struct i40e_hw_port_stats 	stats;
	struct i40e_hw_port_stats	stats_offsets;
	bool 				stat_offsets_loaded;

	/* I2C access methods */
	u8			i2c_access_method;
	s32	(*read_i2c_byte)(struct ixl_pf *pf, u8 byte_offset,
	    u8 dev_addr, u8 *data);
	s32	(*write_i2c_byte)(struct ixl_pf *pf, u8 byte_offset,
	    u8 dev_addr, u8 data);

	/* SR-IOV */
	struct ixl_vf		*vfs;
	int			num_vfs;
	uint16_t		veb_seid;
};

/*
 * Defines used for NVM update ioctls.
 * This value is used in the Solaris tool, too.
 */
#define I40E_NVM_ACCESS \
     (((((((('E' << 4) + '1') << 4) + 'K') << 4) + 'G') << 4) | 5)

#define IXL_DEFAULT_PHY_INT_MASK \
     ((~(I40E_AQ_EVENT_LINK_UPDOWN | I40E_AQ_EVENT_MODULE_QUAL_FAIL \
      | I40E_AQ_EVENT_MEDIA_NA)) & 0x3FF)

/*** Sysctl help messages; displayed with "sysctl -d" ***/

#define IXL_SYSCTL_HELP_SET_ADVERTISE	\
"\nControl advertised link speed.\n"	\
"Flags:\n"				\
"\t 0x1 - advertise 100M\n"		\
"\t 0x2 - advertise 1G\n"		\
"\t 0x4 - advertise 10G\n"		\
"\t 0x8 - advertise 20G\n"		\
"\t0x10 - advertise 25G\n"		\
"\t0x20 - advertise 40G\n\n"		\
"Set to 0 to disable link.\n"		\
"Use \"sysctl -x\" to view flags properly."

#define IXL_SYSCTL_HELP_SUPPORTED_SPEED	\
"\nSupported link speeds.\n"		\
"Flags:\n"				\
"\t 0x1 - 100M\n"			\
"\t 0x2 - 1G\n"				\
"\t 0x4 - 10G\n"			\
"\t 0x8 - 20G\n"			\
"\t0x10 - 25G\n"			\
"\t0x20 - 40G\n\n"			\
"Use \"sysctl -x\" to view flags properly."

#define IXL_SYSCTL_HELP_FC				\
"\nSet flow control mode using the values below.\n" 	\
"\t0 - off\n" 						\
"\t1 - rx pause\n" 					\
"\t2 - tx pause\n"					\
"\t3 - tx and rx pause"

#define IXL_SYSCTL_HELP_LINK_STATUS					\
"\nExecutes a \"Get Link Status\" command on the Admin Queue, and displays" \
" the response."

#define IXL_SYSCTL_HELP_FW_LLDP		\
"\nFW LLDP engine:\n"			\
"\t0 - disable\n"			\
"\t1 - enable\n"

#define IXL_SYSCTL_HELP_READ_I2C		\
"\nRead a byte from I2C bus\n"			\
"Input: 32-bit value\n"				\
"\tbits 0-7:   device address (0xA0 or 0xA2)\n"	\
"\tbits 8-15:  offset (0-255)\n"		\
"\tbits 16-31: unused\n"			\
"Output: 8-bit value read"

#define IXL_SYSCTL_HELP_WRITE_I2C		\
"\nWrite a byte to the I2C bus\n"		\
"Input: 32-bit value\n"				\
"\tbits 0-7:   device address (0xA0 or 0xA2)\n"	\
"\tbits 8-15:  offset (0-255)\n"		\
"\tbits 16-23: value to write\n"		\
"\tbits 24-31: unused\n"			\
"Output: 8-bit value written"

#define IXL_SYSCTL_HELP_I2C_METHOD		\
"\nI2C access method that driver will use:\n"	\
"\t0 - best available method\n"			\
"\t1 - bit bang via I2CPARAMS register\n"	\
"\t2 - register read/write via I2CCMD register\n" \
"\t3 - Use Admin Queue command (best)\n"	\
"Using the Admin Queue is only supported on 710 devices with FW version 1.7 or higher"

#define IXL_SYSCTL_HELP_VF_LOOPBACK		\
"\nDetermines mode that embedded device switch will use when SR-IOV is initialized:\n"	\
"\t0 - Disable (VEPA)\n"			\
"\t1 - Enable (VEB)\n"				\
"Enabling this will allow VFs in separate VMs to communicate over the hardware bridge."

extern const char * const ixl_fc_string[6];

MALLOC_DECLARE(M_IXL);

/*** Functions / Macros ***/
/* Adjust the level here to 10 or over to print stats messages */
#define	I40E_VC_DEBUG(p, level, ...)				\
	do {							\
		if (level < 10)					\
			ixl_dbg(p, IXL_DBG_IOV_VC, ##__VA_ARGS__); \
	} while (0)

#define	i40e_send_vf_nack(pf, vf, op, st) \
	ixl_send_vf_nack_msg((pf), (vf), (op), (st), __FILE__, __LINE__)

/* Debug printing */
#define ixl_dbg(pf, m, s, ...) ixl_debug_core(pf->dev, pf->dbg_mask, m, s, ##__VA_ARGS__)
#define ixl_dbg_info(pf, s, ...) ixl_debug_core(pf->dev, pf->dbg_mask, IXL_DBG_INFO, s, ##__VA_ARGS__)
#define ixl_dbg_filter(pf, s, ...) ixl_debug_core(pf->dev, pf->dbg_mask, IXL_DBG_FILTER, s, ##__VA_ARGS__)
#define ixl_dbg_iov(pf, s, ...) ixl_debug_core(pf->dev, pf->dbg_mask, IXL_DBG_IOV, s, ##__VA_ARGS__)

/* PF-only function declarations */
int	ixl_setup_interface(device_t, struct ixl_pf *);
void	ixl_print_nvm_cmd(device_t, struct i40e_nvm_access *);
char *	ixl_aq_speed_to_str(enum i40e_aq_link_speed);

void	ixl_handle_que(void *context, int pending);

void	ixl_init(void *);
void	ixl_local_timer(void *);
void	ixl_register_vlan(void *, struct ifnet *, u16);
void	ixl_unregister_vlan(void *, struct ifnet *, u16);
int	ixl_intr(void *);
int	ixl_msix_que(void *);
int	ixl_msix_adminq(void *);
void	ixl_do_adminq(void *, int);

int	ixl_res_alloc_cmp(const void *, const void *);
char *	ixl_switch_res_type_string(u8);
char *	ixl_switch_element_string(struct sbuf *,
	    struct i40e_aqc_switch_config_element_resp *);
void	ixl_add_sysctls_mac_stats(struct sysctl_ctx_list *,
		    struct sysctl_oid_list *, struct i40e_hw_port_stats *);

void    ixl_media_status(struct ifnet *, struct ifmediareq *);
int     ixl_media_change(struct ifnet *);
int     ixl_ioctl(struct ifnet *, u_long, caddr_t);

void	ixl_enable_queue(struct i40e_hw *, int);
void	ixl_disable_queue(struct i40e_hw *, int);
void	ixl_enable_intr0(struct i40e_hw *);
void	ixl_disable_intr0(struct i40e_hw *);
void	ixl_nvm_version_str(struct i40e_hw *hw, struct sbuf *buf);
void	ixl_stat_update48(struct i40e_hw *, u32, u32, bool,
		    u64 *, u64 *);
void	ixl_stat_update32(struct i40e_hw *, u32, bool,
		    u64 *, u64 *);

void	ixl_stop(struct ixl_pf *);
int	ixl_get_hw_capabilities(struct ixl_pf *);
void	ixl_link_up_msg(struct ixl_pf *);
void    ixl_update_link_status(struct ixl_pf *);
int	ixl_setup_stations(struct ixl_pf *);
int	ixl_switch_config(struct ixl_pf *);
void	ixl_stop_locked(struct ixl_pf *);
int	ixl_teardown_hw_structs(struct ixl_pf *);
int	ixl_reset(struct ixl_pf *);
void	ixl_init_locked(struct ixl_pf *);
void	ixl_set_rss_key(struct ixl_pf *);
void	ixl_set_rss_pctypes(struct ixl_pf *);
void	ixl_set_rss_hlut(struct ixl_pf *);
int	ixl_setup_adminq_msix(struct ixl_pf *);
int	ixl_setup_adminq_tq(struct ixl_pf *);
int	ixl_teardown_adminq_msix(struct ixl_pf *);
void	ixl_configure_intr0_msix(struct ixl_pf *);
void	ixl_configure_queue_intr_msix(struct ixl_pf *);
void	ixl_free_adminq_tq(struct ixl_pf *);
int	ixl_setup_legacy(struct ixl_pf *);
int	ixl_init_msix(struct ixl_pf *);
void	ixl_configure_itr(struct ixl_pf *);
void	ixl_configure_legacy(struct ixl_pf *);
void	ixl_free_pci_resources(struct ixl_pf *);
void	ixl_link_event(struct ixl_pf *, struct i40e_arq_event_info *);
void	ixl_config_rss(struct ixl_pf *);
int	ixl_set_advertised_speeds(struct ixl_pf *, int, bool);
void	ixl_set_initial_advertised_speeds(struct ixl_pf *);
void	ixl_print_nvm_version(struct ixl_pf *pf);
void	ixl_add_device_sysctls(struct ixl_pf *);
void	ixl_handle_mdd_event(struct ixl_pf *);
void	ixl_add_hw_stats(struct ixl_pf *);
void	ixl_update_stats_counters(struct ixl_pf *);
void	ixl_pf_reset_stats(struct ixl_pf *);
void	ixl_get_bus_info(struct ixl_pf *pf);
int	ixl_aq_get_link_status(struct ixl_pf *,
    struct i40e_aqc_get_link_status *);

int	ixl_handle_nvmupd_cmd(struct ixl_pf *, struct ifdrv *);
void	ixl_handle_empr_reset(struct ixl_pf *);
int	ixl_prepare_for_reset(struct ixl_pf *pf, bool is_up);
int	ixl_rebuild_hw_structs_after_reset(struct ixl_pf *);

void	ixl_set_queue_rx_itr(struct ixl_rx_queue *);
void	ixl_set_queue_tx_itr(struct ixl_tx_queue *);

void	ixl_add_filter(struct ixl_vsi *, const u8 *, s16 vlan);
void	ixl_del_filter(struct ixl_vsi *, const u8 *, s16 vlan);
void	ixl_reconfigure_filters(struct ixl_vsi *vsi);

int	ixl_disable_rings(struct ixl_pf *, struct ixl_vsi *, struct ixl_pf_qtag *);
int	ixl_disable_tx_ring(struct ixl_pf *, struct ixl_pf_qtag *, u16);
int	ixl_disable_rx_ring(struct ixl_pf *, struct ixl_pf_qtag *, u16);
int	ixl_disable_ring(struct ixl_pf *pf, struct ixl_pf_qtag *, u16);

int	ixl_enable_rings(struct ixl_vsi *);
int	ixl_enable_tx_ring(struct ixl_pf *, struct ixl_pf_qtag *, u16);
int	ixl_enable_rx_ring(struct ixl_pf *, struct ixl_pf_qtag *, u16);
int	ixl_enable_ring(struct ixl_pf *pf, struct ixl_pf_qtag *, u16);

void	ixl_update_eth_stats(struct ixl_vsi *);
void	ixl_cap_txcsum_tso(struct ixl_vsi *, struct ifnet *, int);
int	ixl_initialize_vsi(struct ixl_vsi *);
void	ixl_add_ifmedia(struct ixl_vsi *, u64);
int	ixl_setup_queue_msix(struct ixl_vsi *);
int	ixl_setup_queue_tqs(struct ixl_vsi *);
int	ixl_teardown_queue_msix(struct ixl_vsi *);
void	ixl_free_queue_tqs(struct ixl_vsi *);
void	ixl_enable_intr(struct ixl_vsi *);
void	ixl_disable_rings_intr(struct ixl_vsi *);
void	ixl_set_promisc(struct ixl_vsi *);
void	ixl_add_multi(struct ixl_vsi *);
int	ixl_del_multi(struct ixl_vsi *);
void	ixl_setup_vlan_filters(struct ixl_vsi *);
void	ixl_init_filters(struct ixl_vsi *);
void	ixl_add_hw_filters(struct ixl_vsi *, int, int);
void	ixl_del_hw_filters(struct ixl_vsi *, int);
void	ixl_del_default_hw_filters(struct ixl_vsi *);
struct ixl_mac_filter *
		ixl_find_filter(struct ixl_vsi *, const u8 *, s16);
void	ixl_add_mc_filter(struct ixl_vsi *, u8 *);
void	ixl_free_mac_filters(struct ixl_vsi *vsi);
void	ixl_update_vsi_stats(struct ixl_vsi *);
void	ixl_vsi_reset_stats(struct ixl_vsi *);

void	ixl_vsi_free_queues(struct ixl_vsi *vsi);

void	 ixl_if_init(if_ctx_t ctx);
void	 ixl_if_stop(if_ctx_t ctx);

/*
 * I2C Function prototypes
 */
int	ixl_find_i2c_interface(struct ixl_pf *);
s32	ixl_read_i2c_byte_bb(struct ixl_pf *pf, u8 byte_offset,
	    u8 dev_addr, u8 *data);
s32	ixl_write_i2c_byte_bb(struct ixl_pf *pf, u8 byte_offset,
	    u8 dev_addr, u8 data);
s32	ixl_read_i2c_byte_reg(struct ixl_pf *pf, u8 byte_offset,
	    u8 dev_addr, u8 *data);
s32	ixl_write_i2c_byte_reg(struct ixl_pf *pf, u8 byte_offset,
	    u8 dev_addr, u8 data);
s32	ixl_read_i2c_byte_aq(struct ixl_pf *pf, u8 byte_offset,
	    u8 dev_addr, u8 *data);
s32	ixl_write_i2c_byte_aq(struct ixl_pf *pf, u8 byte_offset,
	    u8 dev_addr, u8 data);

int	ixl_get_fw_lldp_status(struct ixl_pf *pf);
int	ixl_attach_get_link_status(struct ixl_pf *);
u64		ixl_max_aq_speed_to_value(u8);

#endif /* _IXL_PF_H_ */
