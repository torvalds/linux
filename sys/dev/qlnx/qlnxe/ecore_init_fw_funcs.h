/*
 * Copyright (c) 2017-2018 Cavium, Inc. 
 * All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

#ifndef _INIT_FW_FUNCS_H
#define _INIT_FW_FUNCS_H
/* Forward declarations */

struct init_qm_pq_params;

/**
 * @brief ecore_qm_pf_mem_size - Prepare QM ILT sizes
 *
 * Returns the required host memory size in 4KB units.
 * Must be called before all QM init HSI functions.
 *
 * @param num_pf_cids - number of connections used by this PF
 * @param num_vf_cids -	number of connections used by VFs of this PF
 * @param num_tids -	number of tasks used by this PF
 * @param num_pf_pqs -	number of PQs used by this PF
 * @param num_vf_pqs -	number of PQs used by VFs of this PF
 *
 * @return The required host memory size in 4KB units.
 */
u32 ecore_qm_pf_mem_size(u32 num_pf_cids,
						 u32 num_vf_cids,
						 u32 num_tids,
						 u16 num_pf_pqs,
						 u16 num_vf_pqs);

/**
 * @brief ecore_qm_common_rt_init - Prepare QM runtime init values for the
 * engine phase.
 *
 * @param p_hwfn -			  HW device data
 * @param max_ports_per_engine -  max number of ports per engine in HW
 * @param max_phys_tcs_per_port	- max number of physical TCs per port in HW
 * @param pf_rl_en -		  enable per-PF rate limiters
 * @param pf_wfq_en -		  enable per-PF WFQ
 * @param vport_rl_en -		  enable per-VPORT rate limiters
 * @param vport_wfq_en -	  enable per-VPORT WFQ
 * @param port_params -		  array of size MAX_NUM_PORTS with parameters
 *				  for each port
 *
 * @return 0 on success, -1 on error.
 */
int ecore_qm_common_rt_init(struct ecore_hwfn *p_hwfn,
							u8 max_ports_per_engine,
							u8 max_phys_tcs_per_port,
							bool pf_rl_en,
							bool pf_wfq_en,
							bool vport_rl_en,
							bool vport_wfq_en,
							struct init_qm_port_params port_params[MAX_NUM_PORTS]);

/**
 * @brief ecore_qm_pf_rt_init - Prepare QM runtime init values for the PF phase
 *
 * @param p_hwfn -			  HW device data
 * @param p_ptt -			  ptt window used for writing the registers
 * @param port_id -		  port ID
 * @param pf_id -		  PF ID
 * @param max_phys_tcs_per_port	- max number of physical TCs per port in HW
 * @param is_pf_loading -	  indicates if the PF is currently loading,
 *				  i.e. it has no allocated QM resources.
 * @param num_pf_cids -		  number of connections used by this PF
 * @param num_vf_cids -		  number of connections used by VFs of this PF
 * @param num_tids -		  number of tasks used by this PF
 * @param start_pq -		  first Tx PQ ID associated with this PF
 * @param num_pf_pqs -		  number of Tx PQs associated with this PF
 *				  (non-VF)
 * @param num_vf_pqs -		  number of Tx PQs associated with a VF
 * @param start_vport -		  first VPORT ID associated with this PF
 * @param num_vports -		  number of VPORTs associated with this PF
 * @param pf_wfq -		  WFQ weight. if PF WFQ is globally disabled,
 *				  the weight must be 0. otherwise, the weight
 *				  must be non-zero.
 * @param pf_rl -		  rate limit in Mb/sec units. a value of 0
 *				  means don't configure. ignored if PF RL is
 *				  globally disabled.
 * @param link_speed -		  link speed in Mbps.
 * @param pq_params -		  array of size (num_pf_pqs + num_vf_pqs) with
 *				  parameters for each Tx PQ associated with the
 *				  specified PF.
 * @param vport_params -	  array of size num_vports with parameters for
 *				  each associated VPORT.
 *
 * @return 0 on success, -1 on error.
 */
int ecore_qm_pf_rt_init(struct ecore_hwfn *p_hwfn,
						struct ecore_ptt *p_ptt,
						u8 port_id,
						u8 pf_id,
						u8 max_phys_tcs_per_port,
						bool is_pf_loading,
						u32 num_pf_cids,
						u32 num_vf_cids,
						u32 num_tids,
						u16 start_pq,
						u16 num_pf_pqs,
						u16 num_vf_pqs,
						u8 start_vport,
						u8 num_vports,
						u16 pf_wfq,
						u32 pf_rl,
						u32 link_speed,
						struct init_qm_pq_params *pq_params,
						struct init_qm_vport_params *vport_params);

/**
 * @brief ecore_init_pf_wfq - Initializes the WFQ weight of the specified PF
 *
 * @param p_hwfn -	   HW device data
 * @param p_ptt -	   ptt window used for writing the registers
 * @param pf_id	-  PF ID
 * @param pf_wfq - WFQ weight. Must be non-zero.
 *
 * @return 0 on success, -1 on error.
 */
int ecore_init_pf_wfq(struct ecore_hwfn *p_hwfn,
					  struct ecore_ptt *p_ptt,
					  u8 pf_id,
					  u16 pf_wfq);

/**
 * @brief ecore_init_pf_rl - Initializes the rate limit of the specified PF
 *
 * @param p_hwfn
 * @param p_ptt -   ptt window used for writing the registers
 * @param pf_id	- PF ID
 * @param pf_rl	- rate limit in Mb/sec units
 *
 * @return 0 on success, -1 on error.
 */
int ecore_init_pf_rl(struct ecore_hwfn *p_hwfn,
					 struct ecore_ptt *p_ptt,
					 u8 pf_id,
					 u32 pf_rl);

/**
 * @brief ecore_init_vport_wfq - Initializes the WFQ weight of the specified VPORT
 *
 * @param p_hwfn -		   HW device data
 * @param p_ptt -		   ptt window used for writing the registers
 * @param first_tx_pq_id - An array containing the first Tx PQ ID associated
 *                         with the VPORT for each TC. This array is filled by
 *                         ecore_qm_pf_rt_init
 * @param vport_wfq -	   WFQ weight. Must be non-zero.
 *
 * @return 0 on success, -1 on error.
 */
int ecore_init_vport_wfq(struct ecore_hwfn *p_hwfn,
						 struct ecore_ptt *p_ptt,
						 u16 first_tx_pq_id[NUM_OF_TCS],
						 u16 vport_wfq);

/**
 * @brief ecore_init_vport_rl - Initializes the rate limit of the specified
 * VPORT.
 *
 * @param p_hwfn -	       HW device data
 * @param p_ptt -	       ptt window used for writing the registers
 * @param vport_id -   VPORT ID
 * @param vport_rl -   rate limit in Mb/sec units
 * @param link_speed - link speed in Mbps.
 *
 * @return 0 on success, -1 on error.
 */
int ecore_init_vport_rl(struct ecore_hwfn *p_hwfn,
						struct ecore_ptt *p_ptt,
						u8 vport_id,
						u32 vport_rl,
						u32 link_speed);

/**
 * @brief ecore_send_qm_stop_cmd - Sends a stop command to the QM
 *
 * @param p_hwfn -		   HW device data
 * @param p_ptt -		   ptt window used for writing the registers
 * @param is_release_cmd - true for release, false for stop.
 * @param is_tx_pq -	   true for Tx PQs, false for Other PQs.
 * @param start_pq -	   first PQ ID to stop
 * @param num_pqs -	   Number of PQs to stop, starting from start_pq.
 *
 * @return bool, true if successful, false if timeout occured while waiting for
 * QM command done.
 */
bool ecore_send_qm_stop_cmd(struct ecore_hwfn *p_hwfn,
							struct ecore_ptt *p_ptt,
							bool is_release_cmd,
							bool is_tx_pq,
							u16 start_pq,
							u16 num_pqs);

#ifndef UNUSED_HSI_FUNC

/**
 * @brief ecore_init_nig_ets - Initializes the NIG ETS arbiter
 *
 * Based on weight/priority requirements per-TC.
 *
 * @param p_hwfn -   HW device data
 * @param p_ptt -   ptt window used for writing the registers.
 * @param req -   the NIG ETS initialization requirements.
 * @param is_lb	- if set, the loopback port arbiter is initialized, otherwise
 *		  the physical port arbiter is initialized. The pure-LB TC
 *		  requirements are ignored when is_lb is cleared.
 */
void ecore_init_nig_ets(struct ecore_hwfn *p_hwfn,
						struct ecore_ptt *p_ptt,
						struct init_ets_req* req,
						bool is_lb);

/**
 * @brief ecore_init_nig_lb_rl - Initializes the NIG LB RLs
 *
 * Based on global and per-TC rate requirements
 *
 * @param p_hwfn -	HW device data
 * @param p_ptt - ptt window used for writing the registers.
 * @param req -	the NIG LB RLs initialization requirements.
 */
void ecore_init_nig_lb_rl(struct ecore_hwfn *p_hwfn,
						  struct ecore_ptt *p_ptt,
						  struct init_nig_lb_rl_req* req);

#endif /* UNUSED_HSI_FUNC */

/**
 * @brief ecore_init_nig_pri_tc_map - Initializes the NIG priority to TC map.
 *
 * Assumes valid arguments.
 *
 * @param p_hwfn -	HW device data
 * @param p_ptt - ptt window used for writing the registers.
 * @param req - required mapping from prioirties to TCs.
 */
void ecore_init_nig_pri_tc_map(struct ecore_hwfn *p_hwfn,
							   struct ecore_ptt *p_ptt,
							   struct init_nig_pri_tc_map_req* req);

#ifndef UNUSED_HSI_FUNC

/**
 * @brief ecore_init_prs_ets - Initializes the PRS Rx ETS arbiter
 *
 * Based on weight/priority requirements per-TC.
 *
 * @param p_hwfn -	HW device data
 * @param p_ptt - ptt window used for writing the registers.
 * @param req -	the PRS ETS initialization requirements.
 */
void ecore_init_prs_ets(struct ecore_hwfn *p_hwfn,
						struct ecore_ptt *p_ptt,
						struct init_ets_req* req);

#endif /* UNUSED_HSI_FUNC */
#ifndef UNUSED_HSI_FUNC

/**
 * @brief ecore_init_brb_ram - Initializes BRB RAM sizes per TC.
 *
 * Based on weight/priority requirements per-TC.
 *
 * @param p_hwfn -   HW device data
 * @param p_ptt	- ptt window used for writing the registers.
 * @param req -   the BRB RAM initialization requirements.
 */
void ecore_init_brb_ram(struct ecore_hwfn *p_hwfn,
						struct ecore_ptt *p_ptt,
						struct init_brb_ram_req* req);

#endif /* UNUSED_HSI_FUNC */
#ifndef UNUSED_HSI_FUNC

/**
 * @brief ecore_set_port_mf_ovlan_eth_type - initializes DORQ ethType Regs to
 * input ethType. should Be called once per port.
 *
 * @param p_hwfn -     HW device data
 * @param ethType - etherType to configure
 */
void ecore_set_port_mf_ovlan_eth_type(struct ecore_hwfn *p_hwfn,
									  u32 ethType);

#endif /* UNUSED_HSI_FUNC */

/**
 * @brief ecore_set_vxlan_dest_port - Initializes vxlan tunnel destination udp
 * port.
 *
 * @param p_hwfn -	      HW device data
 * @param p_ptt -       ptt window used for writing the registers.
 * @param dest_port - vxlan destination udp port.
 */
void ecore_set_vxlan_dest_port(struct ecore_hwfn *p_hwfn,
                               struct ecore_ptt *p_ptt,
                               u16 dest_port);

/**
 * @brief ecore_set_vxlan_enable - Enable or disable VXLAN tunnel in HW
 *
 * @param p_hwfn -		 HW device data
 * @param p_ptt -		 ptt window used for writing the registers.
 * @param vxlan_enable - vxlan enable flag.
 */
void ecore_set_vxlan_enable(struct ecore_hwfn *p_hwfn,
                            struct ecore_ptt *p_ptt,
                            bool vxlan_enable);

/**
 * @brief ecore_set_gre_enable - Enable or disable GRE tunnel in HW
 *
 * @param p_hwfn -		   HW device data
 * @param p_ptt -		   ptt window used for writing the registers.
 * @param eth_gre_enable - eth GRE enable enable flag.
 * @param ip_gre_enable -  IP GRE enable enable flag.
 */
void ecore_set_gre_enable(struct ecore_hwfn *p_hwfn,
                          struct ecore_ptt *p_ptt,
                          bool eth_gre_enable,
                          bool ip_gre_enable);

/**
 * @brief ecore_set_geneve_dest_port - Initializes geneve tunnel destination
 * udp port.
 *
 * @param p_hwfn -	      HW device data
 * @param p_ptt -       ptt window used for writing the registers.
 * @param dest_port - geneve destination udp port.
 */
void ecore_set_geneve_dest_port(struct ecore_hwfn *p_hwfn,
                                struct ecore_ptt *p_ptt,
                                u16 dest_port);

/**
 * @brief ecore_set_geneve_enable - Enable or disable GRE tunnel in HW
 *
 * @param p_hwfn -			HW device data
 * @param p_ptt -			ptt window used for writing the registers.
 * @param eth_geneve_enable -	eth GENEVE enable enable flag.
 * @param ip_geneve_enable -	IP GENEVE enable enable flag.
  */
void ecore_set_geneve_enable(struct ecore_hwfn *p_hwfn,
                             struct ecore_ptt *p_ptt,
                             bool eth_geneve_enable,
                             bool ip_geneve_enable);

/**
* @brief ecore_set_vxlan_no_l2_enable - enable or disable VXLAN no L2 parsing
*
* @param p_ptt             - ptt window used for writing the registers.
* @param enable            - VXLAN no L2 enable flag.
*/
void ecore_set_vxlan_no_l2_enable(struct ecore_hwfn *p_hwfn,
    struct ecore_ptt *p_ptt,
    bool enable);

#ifndef UNUSED_HSI_FUNC

/**
 * @brief ecore_set_gft_event_id_cm_hdr - Configure GFT event id and cm header
 *
 * @param p_hwfn - HW device data
 * @param p_ptt - ptt window used for writing the registers.
 */
void ecore_set_gft_event_id_cm_hdr(struct ecore_hwfn *p_hwfn,
								   struct ecore_ptt *p_ptt);

/**
 * @brief ecore_gft_disable - Disable and GFT
 *
 * @param p_hwfn -   HW device data
 * @param p_ptt -   ptt window used for writing the registers.
 * @param pf_id - pf on which to disable GFT.
 */
void ecore_gft_disable(struct ecore_hwfn *p_hwfn,
						struct ecore_ptt *p_ptt,
						u16 pf_id);

/**
 * @brief ecore_gft_config - Enable and configure HW for GFT
 *                           
 * @param p_hwfn -	  HW device data
 * @param p_ptt -   ptt window used for writing the registers.
 * @param pf_id - pf on which to enable GFT.
 * @param tcp -   set profile tcp packets.
 * @param udp -   set profile udp  packet.
 * @param ipv4 -  set profile ipv4 packet.
 * @param ipv6 -  set profile ipv6 packet.
 * @param profile_type -  define packet same fields. Use enum gft_profile_type.
 */
void ecore_gft_config(struct ecore_hwfn *p_hwfn,
	struct ecore_ptt *p_ptt,
	u16 pf_id,
	bool tcp,
	bool udp,
	bool ipv4,
	bool ipv6,
    enum gft_profile_type profile_type);

#endif /* UNUSED_HSI_FUNC */

/**
 * @brief ecore_config_vf_zone_size_mode - Configure VF zone size mode. Must be
 * used before first ETH queue started.
 *
 * @param p_hwfn -		 HW device data
 * @param p_ptt -		 ptt window used for writing the registers. Don't care
 *			 if runtime_init used.
 * @param mode -	 VF zone size mode. Use enum vf_zone_size_mode.
 * @param runtime_init - Set 1 to init runtime registers in engine phase.
 *			 Set 0 if VF zone size mode configured after engine
 *			 phase.
 */
void ecore_config_vf_zone_size_mode(struct ecore_hwfn *p_hwfn,
									struct ecore_ptt *p_ptt,
									u16 mode,
									bool runtime_init);

/**
 * @brief ecore_get_mstorm_queue_stat_offset - Get mstorm statistics offset by
 * VF zone size mode.
 *
 * @param p_hwfn -			HW device data
 * @param stat_cnt_id -		statistic counter id
 * @param vf_zone_size_mode -	VF zone size mode. Use enum vf_zone_size_mode.
 */
u32 ecore_get_mstorm_queue_stat_offset(struct ecore_hwfn *p_hwfn,
									   u16 stat_cnt_id,
									   u16 vf_zone_size_mode);

/**
 * @brief ecore_get_mstorm_eth_vf_prods_offset - VF producer offset by VF zone
 * size mode.
 *
 * @param p_hwfn -		      HW device data
 * @param vf_id -	      vf id.
 * @param vf_queue_id -	      per VF rx queue id.
 * @param vf_zone_size_mode - vf zone size mode. Use enum vf_zone_size_mode.
 */
u32 ecore_get_mstorm_eth_vf_prods_offset(struct ecore_hwfn *p_hwfn,
										 u8 vf_id,
										 u8 vf_queue_id,
										 u16 vf_zone_size_mode);

/**
 * @brief ecore_enable_context_validation - Enable and configure context
 * validation.
 *
 * @param p_hwfn -   HW device data
 * @param p_ptt - ptt window used for writing the registers.
 */
void ecore_enable_context_validation(struct ecore_hwfn *p_hwfn,
									 struct ecore_ptt *p_ptt);

/**
 * @brief ecore_calc_session_ctx_validation - Calcualte validation byte for
 * session context.
 *
 * @param p_ctx_mem -	pointer to context memory.
 * @param ctx_size -	context size.
 * @param ctx_type -	context type.
 * @param cid -		context cid.
 */
void ecore_calc_session_ctx_validation(void *p_ctx_mem,
				       u16 ctx_size,
				       u8 ctx_type,
				       u32 cid);

/**
 * @brief ecore_calc_task_ctx_validation - Calcualte validation byte for task
 * context.
 *
 * @param p_ctx_mem -	pointer to context memory.
 * @param ctx_size -	context size.
 * @param ctx_type -	context type.
 * @param tid -		    context tid.
 */
void ecore_calc_task_ctx_validation(void *p_ctx_mem,
				    u16 ctx_size,
				    u8 ctx_type,
				    u32 tid);

/**
 * @brief ecore_memset_session_ctx - Memset session context to 0 while
 * preserving validation bytes.
 *
 * @param p_hwfn -		  HW device data
 * @param p_ctx_mem - pointer to context memory.
 * @param ctx_size -  size to initialzie.
 * @param ctx_type -  context type.
 */
void ecore_memset_session_ctx(void *p_ctx_mem,
			      u32 ctx_size,
			      u8 ctx_type);

/**
 * @brief ecore_memset_task_ctx - Memset task context to 0 while preserving
 * validation bytes.
 *
 * @param p_ctx_mem - pointer to context memory.
 * @param ctx_size -  size to initialzie.
 * @param ctx_type -  context type.
 */
void ecore_memset_task_ctx(void *p_ctx_mem,
			   u32 ctx_size,
			   u8 ctx_type);

/**
* @brief ecore_update_eth_rss_ind_table_entry - Update RSS indirection table entry.
* The function must run in exclusive mode to prevent wrong RSS configuration.
*                
* @param p_hwfn    - HW device data
* @param p_ptt  - ptt window used for writing the registers.
* @param rss_id - RSS engine ID.
* @param ind_table_index -  RSS indirect table index.
* @param ind_table_value -  RSS indirect table new value.
*/
void ecore_update_eth_rss_ind_table_entry(struct ecore_hwfn * p_hwfn,
                                          struct ecore_ptt *p_ptt,
                                          u8 rss_id,
                                          u8 ind_table_index,
                                          u16 ind_table_value);

#endif
