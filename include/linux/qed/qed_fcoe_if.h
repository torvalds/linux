#ifndef _QED_FCOE_IF_H
#define _QED_FCOE_IF_H
#include <linux/types.h>
#include <linux/qed/qed_if.h>
struct qed_fcoe_stats {
	u64 fcoe_rx_byte_cnt;
	u64 fcoe_rx_data_pkt_cnt;
	u64 fcoe_rx_xfer_pkt_cnt;
	u64 fcoe_rx_other_pkt_cnt;
	u32 fcoe_silent_drop_pkt_cmdq_full_cnt;
	u32 fcoe_silent_drop_pkt_rq_full_cnt;
	u32 fcoe_silent_drop_pkt_crc_error_cnt;
	u32 fcoe_silent_drop_pkt_task_invalid_cnt;
	u32 fcoe_silent_drop_total_pkt_cnt;

	u64 fcoe_tx_byte_cnt;
	u64 fcoe_tx_data_pkt_cnt;
	u64 fcoe_tx_xfer_pkt_cnt;
	u64 fcoe_tx_other_pkt_cnt;
};

struct qed_dev_fcoe_info {
	struct qed_dev_info common;

	void __iomem *primary_dbq_rq_addr;
	void __iomem *secondary_bdq_rq_addr;

	u64 wwpn;
	u64 wwnn;

	u8 num_cqs;
};

struct qed_fcoe_params_offload {
	dma_addr_t sq_pbl_addr;
	dma_addr_t sq_curr_page_addr;
	dma_addr_t sq_next_page_addr;

	u8 src_mac[ETH_ALEN];
	u8 dst_mac[ETH_ALEN];

	u16 tx_max_fc_pay_len;
	u16 e_d_tov_timer_val;
	u16 rec_tov_timer_val;
	u16 rx_max_fc_pay_len;
	u16 vlan_tag;

	struct fc_addr_nw s_id;
	u8 max_conc_seqs_c3;
	struct fc_addr_nw d_id;
	u8 flags;
	u8 def_q_idx;
};

#define MAX_TID_BLOCKS_FCOE (512)
struct qed_fcoe_tid {
	u32 size;		/* In bytes per task */
	u32 num_tids_per_block;
	u8 *blocks[MAX_TID_BLOCKS_FCOE];
};

struct qed_fcoe_cb_ops {
	struct qed_common_cb_ops common;
	 u32 (*get_login_failures)(void *cookie);
};

void qed_fcoe_set_pf_params(struct qed_dev *cdev,
			    struct qed_fcoe_pf_params *params);

/**
 * struct qed_fcoe_ops - qed FCoE operations.
 * @common:		common operations pointer
 * @fill_dev_info:	fills FCoE specific information
 *			@param cdev
 *			@param info
 *			@return 0 on sucesss, otherwise error value.
 * @register_ops:	register FCoE operations
 *			@param cdev
 *			@param ops - specified using qed_iscsi_cb_ops
 *			@param cookie - driver private
 * @ll2:		light L2 operations pointer
 * @start:		fcoe in FW
 *			@param cdev
 *			@param tasks - qed will fill information about tasks
 *			return 0 on success, otherwise error value.
 * @stop:		stops fcoe in FW
 *			@param cdev
 *			return 0 on success, otherwise error value.
 * @acquire_conn:	acquire a new fcoe connection
 *			@param cdev
 *			@param handle - qed will fill handle that should be
 *				used henceforth as identifier of the
 *				connection.
 *			@param p_doorbell - qed will fill the address of the
 *				doorbell.
 *			return 0 on sucesss, otherwise error value.
 * @release_conn:	release a previously acquired fcoe connection
 *			@param cdev
 *			@param handle - the connection handle.
 *			return 0 on success, otherwise error value.
 * @offload_conn:	configures an offloaded connection
 *			@param cdev
 *			@param handle - the connection handle.
 *			@param conn_info - the configuration to use for the
 *				offload.
 *			return 0 on success, otherwise error value.
 * @destroy_conn:	stops an offloaded connection
 *			@param cdev
 *			@param handle - the connection handle.
 *			@param terminate_params
 *			return 0 on success, otherwise error value.
 * @get_stats:		gets FCoE related statistics
 *			@param cdev
 *			@param stats - pointer to struck that would be filled
 *				we stats
 *			return 0 on success, error otherwise.
 */
struct qed_fcoe_ops {
	const struct qed_common_ops *common;

	int (*fill_dev_info)(struct qed_dev *cdev,
			     struct qed_dev_fcoe_info *info);

	void (*register_ops)(struct qed_dev *cdev,
			     struct qed_fcoe_cb_ops *ops, void *cookie);

	const struct qed_ll2_ops *ll2;

	int (*start)(struct qed_dev *cdev, struct qed_fcoe_tid *tasks);

	int (*stop)(struct qed_dev *cdev);

	int (*acquire_conn)(struct qed_dev *cdev,
			    u32 *handle,
			    u32 *fw_cid, void __iomem **p_doorbell);

	int (*release_conn)(struct qed_dev *cdev, u32 handle);

	int (*offload_conn)(struct qed_dev *cdev,
			    u32 handle,
			    struct qed_fcoe_params_offload *conn_info);
	int (*destroy_conn)(struct qed_dev *cdev,
			    u32 handle, dma_addr_t terminate_params);

	int (*get_stats)(struct qed_dev *cdev, struct qed_fcoe_stats *stats);
};

const struct qed_fcoe_ops *qed_get_fcoe_ops(void);
void qed_put_fcoe_ops(void);
#endif
