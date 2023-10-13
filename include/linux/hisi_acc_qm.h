/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 HiSilicon Limited. */
#ifndef HISI_ACC_QM_H
#define HISI_ACC_QM_H

#include <linux/bitfield.h>
#include <linux/debugfs.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/pci.h>

#define QM_QNUM_V1			4096
#define QM_QNUM_V2			1024
#define QM_MAX_VFS_NUM_V2		63

/* qm user domain */
#define QM_ARUSER_M_CFG_1		0x100088
#define AXUSER_SNOOP_ENABLE		BIT(30)
#define AXUSER_CMD_TYPE			GENMASK(14, 12)
#define AXUSER_CMD_SMMU_NORMAL		1
#define AXUSER_NS			BIT(6)
#define AXUSER_NO			BIT(5)
#define AXUSER_FP			BIT(4)
#define AXUSER_SSV			BIT(0)
#define AXUSER_BASE			(AXUSER_SNOOP_ENABLE |		\
					FIELD_PREP(AXUSER_CMD_TYPE,	\
					AXUSER_CMD_SMMU_NORMAL) |	\
					AXUSER_NS | AXUSER_NO | AXUSER_FP)
#define QM_ARUSER_M_CFG_ENABLE		0x100090
#define ARUSER_M_CFG_ENABLE		0xfffffffe
#define QM_AWUSER_M_CFG_1		0x100098
#define QM_AWUSER_M_CFG_ENABLE		0x1000a0
#define AWUSER_M_CFG_ENABLE		0xfffffffe
#define QM_WUSER_M_CFG_ENABLE		0x1000a8
#define WUSER_M_CFG_ENABLE		0xffffffff

/* mailbox */
#define QM_MB_CMD_SQC                   0x0
#define QM_MB_CMD_CQC                   0x1
#define QM_MB_CMD_EQC                   0x2
#define QM_MB_CMD_AEQC                  0x3
#define QM_MB_CMD_SQC_BT                0x4
#define QM_MB_CMD_CQC_BT                0x5
#define QM_MB_CMD_SQC_VFT_V2            0x6
#define QM_MB_CMD_STOP_QP               0x8
#define QM_MB_CMD_SRC                   0xc
#define QM_MB_CMD_DST                   0xd

#define QM_MB_CMD_SEND_BASE		0x300
#define QM_MB_EVENT_SHIFT               8
#define QM_MB_BUSY_SHIFT		13
#define QM_MB_OP_SHIFT			14
#define QM_MB_CMD_DATA_ADDR_L		0x304
#define QM_MB_CMD_DATA_ADDR_H		0x308
#define QM_MB_MAX_WAIT_CNT		6000

/* doorbell */
#define QM_DOORBELL_CMD_SQ              0
#define QM_DOORBELL_CMD_CQ              1
#define QM_DOORBELL_CMD_EQ              2
#define QM_DOORBELL_CMD_AEQ             3

#define QM_DOORBELL_SQ_CQ_BASE_V2	0x1000
#define QM_DOORBELL_EQ_AEQ_BASE_V2	0x2000
#define QM_QP_MAX_NUM_SHIFT             11
#define QM_DB_CMD_SHIFT_V2		12
#define QM_DB_RAND_SHIFT_V2		16
#define QM_DB_INDEX_SHIFT_V2		32
#define QM_DB_PRIORITY_SHIFT_V2		48
#define QM_VF_STATE			0x60

/* qm cache */
#define QM_CACHE_CTL			0x100050
#define SQC_CACHE_ENABLE		BIT(0)
#define CQC_CACHE_ENABLE		BIT(1)
#define SQC_CACHE_WB_ENABLE		BIT(4)
#define SQC_CACHE_WB_THRD		GENMASK(10, 5)
#define CQC_CACHE_WB_ENABLE		BIT(11)
#define CQC_CACHE_WB_THRD		GENMASK(17, 12)
#define QM_AXI_M_CFG			0x1000ac
#define AXI_M_CFG			0xffff
#define QM_AXI_M_CFG_ENABLE		0x1000b0
#define AM_CFG_SINGLE_PORT_MAX_TRANS	0x300014
#define AXI_M_CFG_ENABLE		0xffffffff
#define QM_PEH_AXUSER_CFG		0x1000cc
#define QM_PEH_AXUSER_CFG_ENABLE	0x1000d0
#define PEH_AXUSER_CFG			0x401001
#define PEH_AXUSER_CFG_ENABLE		0xffffffff

#define QM_MIN_QNUM                     2
#define HISI_ACC_SGL_SGE_NR_MAX		255
#define QM_SHAPER_CFG			0x100164
#define QM_SHAPER_ENABLE		BIT(30)
#define QM_SHAPER_TYPE1_OFFSET		10

/* page number for queue file region */
#define QM_DOORBELL_PAGE_NR		1

/* uacce mode of the driver */
#define UACCE_MODE_NOUACCE		0 /* don't use uacce */
#define UACCE_MODE_SVA			1 /* use uacce sva mode */
#define UACCE_MODE_DESC	"0(default) means only register to crypto, 1 means both register to crypto and uacce"

enum qm_stop_reason {
	QM_NORMAL,
	QM_SOFT_RESET,
	QM_DOWN,
};

enum qm_state {
	QM_INIT = 0,
	QM_START,
	QM_CLOSE,
	QM_STOP,
};

enum qp_state {
	QP_INIT = 1,
	QP_START,
	QP_STOP,
	QP_CLOSE,
};

enum qm_hw_ver {
	QM_HW_V1 = 0x20,
	QM_HW_V2 = 0x21,
	QM_HW_V3 = 0x30,
};

enum qm_fun_type {
	QM_HW_PF,
	QM_HW_VF,
};

enum qm_debug_file {
	CURRENT_QM,
	CURRENT_Q,
	CLEAR_ENABLE,
	DEBUG_FILE_NUM,
};

enum qm_vf_state {
	QM_READY = 0,
	QM_NOT_READY,
};

enum qm_misc_ctl_bits {
	QM_DRIVER_REMOVING = 0x0,
	QM_RST_SCHED,
	QM_RESETTING,
	QM_MODULE_PARAM,
};

enum qm_cap_bits {
	QM_SUPPORT_DB_ISOLATION = 0x0,
	QM_SUPPORT_FUNC_QOS,
	QM_SUPPORT_STOP_QP,
	QM_SUPPORT_MB_COMMAND,
	QM_SUPPORT_SVA_PREFETCH,
	QM_SUPPORT_RPM,
};

struct dfx_diff_registers {
	u32 *regs;
	u32 reg_offset;
	u32 reg_len;
};

struct qm_dfx {
	atomic64_t err_irq_cnt;
	atomic64_t aeq_irq_cnt;
	atomic64_t abnormal_irq_cnt;
	atomic64_t create_qp_err_cnt;
	atomic64_t mb_err_cnt;
};

struct debugfs_file {
	enum qm_debug_file index;
	struct mutex lock;
	struct qm_debug *debug;
};

struct qm_debug {
	u32 curr_qm_qp_num;
	u32 sqe_mask_offset;
	u32 sqe_mask_len;
	struct qm_dfx dfx;
	struct dentry *debug_root;
	struct dentry *qm_d;
	struct debugfs_file files[DEBUG_FILE_NUM];
	unsigned int *qm_last_words;
	/* ACC engines recoreding last regs */
	unsigned int *last_words;
	struct dfx_diff_registers *qm_diff_regs;
	struct dfx_diff_registers *acc_diff_regs;
};

struct qm_shaper_factor {
	u32 func_qos;
	u64 cir_b;
	u64 cir_u;
	u64 cir_s;
	u64 cbs_s;
};

struct qm_dma {
	void *va;
	dma_addr_t dma;
	size_t size;
};

struct hisi_qm_status {
	u32 eq_head;
	bool eqc_phase;
	u32 aeq_head;
	bool aeqc_phase;
	atomic_t flags;
	int stop_reason;
};

struct hisi_qm;

struct hisi_qm_err_info {
	char *acpi_rst;
	u32 msi_wr_port;
	u32 ecc_2bits_mask;
	u32 qm_shutdown_mask;
	u32 dev_shutdown_mask;
	u32 qm_reset_mask;
	u32 dev_reset_mask;
	u32 ce;
	u32 nfe;
	u32 fe;
};

struct hisi_qm_err_status {
	u32 is_qm_ecc_mbit;
	u32 is_dev_ecc_mbit;
};

struct hisi_qm_err_ini {
	int (*hw_init)(struct hisi_qm *qm);
	void (*hw_err_enable)(struct hisi_qm *qm);
	void (*hw_err_disable)(struct hisi_qm *qm);
	u32 (*get_dev_hw_err_status)(struct hisi_qm *qm);
	void (*clear_dev_hw_err_status)(struct hisi_qm *qm, u32 err_sts);
	void (*open_axi_master_ooo)(struct hisi_qm *qm);
	void (*close_axi_master_ooo)(struct hisi_qm *qm);
	void (*open_sva_prefetch)(struct hisi_qm *qm);
	void (*close_sva_prefetch)(struct hisi_qm *qm);
	void (*log_dev_hw_err)(struct hisi_qm *qm, u32 err_sts);
	void (*show_last_dfx_regs)(struct hisi_qm *qm);
	void (*err_info_init)(struct hisi_qm *qm);
};

struct hisi_qm_cap_info {
	u32 type;
	/* Register offset */
	u32 offset;
	/* Bit offset in register */
	u32 shift;
	u32 mask;
	u32 v1_val;
	u32 v2_val;
	u32 v3_val;
};

struct hisi_qm_list {
	struct mutex lock;
	struct list_head list;
	int (*register_to_crypto)(struct hisi_qm *qm);
	void (*unregister_from_crypto)(struct hisi_qm *qm);
};

struct hisi_qm_poll_data {
	struct hisi_qm *qm;
	struct work_struct work;
	u16 *qp_finish_id;
	u16 eqe_num;
};

/**
 * struct qm_err_isolate
 * @isolate_lock: protects device error log
 * @err_threshold: user config error threshold which triggers isolation
 * @is_isolate: device isolation state
 * @uacce_hw_errs: index into qm device error list
 */
struct qm_err_isolate {
	struct mutex isolate_lock;
	u32 err_threshold;
	bool is_isolate;
	struct list_head qm_hw_errs;
};

struct qm_rsv_buf {
	struct qm_sqc *sqc;
	struct qm_cqc *cqc;
	struct qm_eqc *eqc;
	struct qm_aeqc *aeqc;
	dma_addr_t sqc_dma;
	dma_addr_t cqc_dma;
	dma_addr_t eqc_dma;
	dma_addr_t aeqc_dma;
	struct qm_dma qcdma;
};

struct hisi_qm {
	enum qm_hw_ver ver;
	enum qm_fun_type fun_type;
	const char *dev_name;
	struct pci_dev *pdev;
	void __iomem *io_base;
	void __iomem *db_io_base;

	/* Capbility version, 0: not supports */
	u32 cap_ver;
	u32 sqe_size;
	u32 qp_base;
	u32 qp_num;
	u32 qp_in_used;
	u32 ctrl_qp_num;
	u32 max_qp_num;
	u32 vfs_num;
	u32 db_interval;
	u16 eq_depth;
	u16 aeq_depth;
	struct list_head list;
	struct hisi_qm_list *qm_list;

	struct qm_dma qdma;
	struct qm_sqc *sqc;
	struct qm_cqc *cqc;
	struct qm_eqe *eqe;
	struct qm_aeqe *aeqe;
	dma_addr_t sqc_dma;
	dma_addr_t cqc_dma;
	dma_addr_t eqe_dma;
	dma_addr_t aeqe_dma;
	struct qm_rsv_buf xqc_buf;

	struct hisi_qm_status status;
	const struct hisi_qm_err_ini *err_ini;
	struct hisi_qm_err_info err_info;
	struct hisi_qm_err_status err_status;
	/* driver removing and reset sched */
	unsigned long misc_ctl;
	/* Device capability bit */
	unsigned long caps;

	struct rw_semaphore qps_lock;
	struct idr qp_idr;
	struct hisi_qp *qp_array;
	struct hisi_qm_poll_data *poll_data;

	struct mutex mailbox_lock;

	const struct hisi_qm_hw_ops *ops;

	struct qm_debug debug;

	u32 error_mask;

	struct workqueue_struct *wq;
	struct work_struct rst_work;
	struct work_struct cmd_process;

	const char *algs;
	bool use_sva;

	resource_size_t phys_base;
	resource_size_t db_phys_base;
	struct uacce_device *uacce;
	int mode;
	struct qm_shaper_factor *factor;
	u32 mb_qos;
	u32 type_rate;
	struct qm_err_isolate isolate_data;
};

struct hisi_qp_status {
	atomic_t used;
	u16 sq_tail;
	u16 cq_head;
	bool cqc_phase;
	atomic_t flags;
};

struct hisi_qp_ops {
	int (*fill_sqe)(void *sqe, void *q_parm, void *d_parm);
};

struct hisi_qp {
	u32 qp_id;
	u16 sq_depth;
	u16 cq_depth;
	u8 alg_type;
	u8 req_type;

	struct qm_dma qdma;
	void *sqe;
	struct qm_cqe *cqe;
	dma_addr_t sqe_dma;
	dma_addr_t cqe_dma;

	struct hisi_qp_status qp_status;
	struct hisi_qp_ops *hw_ops;
	void *qp_ctx;
	void (*req_cb)(struct hisi_qp *qp, void *data);
	void (*event_cb)(struct hisi_qp *qp);

	struct hisi_qm *qm;
	bool is_resetting;
	bool is_in_kernel;
	u16 pasid;
	struct uacce_queue *uacce_q;
};

static inline int q_num_set(const char *val, const struct kernel_param *kp,
			    unsigned int device)
{
	struct pci_dev *pdev;
	u32 n, q_num;
	int ret;

	if (!val)
		return -EINVAL;

	pdev = pci_get_device(PCI_VENDOR_ID_HUAWEI, device, NULL);
	if (!pdev) {
		q_num = min_t(u32, QM_QNUM_V1, QM_QNUM_V2);
		pr_info("No device found currently, suppose queue number is %u\n",
			q_num);
	} else {
		if (pdev->revision == QM_HW_V1)
			q_num = QM_QNUM_V1;
		else
			q_num = QM_QNUM_V2;

		pci_dev_put(pdev);
	}

	ret = kstrtou32(val, 10, &n);
	if (ret || n < QM_MIN_QNUM || n > q_num)
		return -EINVAL;

	return param_set_int(val, kp);
}

static inline int vfs_num_set(const char *val, const struct kernel_param *kp)
{
	u32 n;
	int ret;

	if (!val)
		return -EINVAL;

	ret = kstrtou32(val, 10, &n);
	if (ret < 0)
		return ret;

	if (n > QM_MAX_VFS_NUM_V2)
		return -EINVAL;

	return param_set_int(val, kp);
}

static inline int mode_set(const char *val, const struct kernel_param *kp)
{
	u32 n;
	int ret;

	if (!val)
		return -EINVAL;

	ret = kstrtou32(val, 10, &n);
	if (ret != 0 || (n != UACCE_MODE_SVA &&
			 n != UACCE_MODE_NOUACCE))
		return -EINVAL;

	return param_set_int(val, kp);
}

static inline int uacce_mode_set(const char *val, const struct kernel_param *kp)
{
	return mode_set(val, kp);
}

static inline void hisi_qm_init_list(struct hisi_qm_list *qm_list)
{
	INIT_LIST_HEAD(&qm_list->list);
	mutex_init(&qm_list->lock);
}

static inline void hisi_qm_add_list(struct hisi_qm *qm, struct hisi_qm_list *qm_list)
{
	mutex_lock(&qm_list->lock);
	list_add_tail(&qm->list, &qm_list->list);
	mutex_unlock(&qm_list->lock);
}

static inline void hisi_qm_del_list(struct hisi_qm *qm, struct hisi_qm_list *qm_list)
{
	mutex_lock(&qm_list->lock);
	list_del(&qm->list);
	mutex_unlock(&qm_list->lock);
}

int hisi_qm_init(struct hisi_qm *qm);
void hisi_qm_uninit(struct hisi_qm *qm);
int hisi_qm_start(struct hisi_qm *qm);
int hisi_qm_stop(struct hisi_qm *qm, enum qm_stop_reason r);
int hisi_qm_start_qp(struct hisi_qp *qp, unsigned long arg);
int hisi_qm_stop_qp(struct hisi_qp *qp);
int hisi_qp_send(struct hisi_qp *qp, const void *msg);
void hisi_qm_debug_init(struct hisi_qm *qm);
void hisi_qm_debug_regs_clear(struct hisi_qm *qm);
int hisi_qm_sriov_enable(struct pci_dev *pdev, int max_vfs);
int hisi_qm_sriov_disable(struct pci_dev *pdev, bool is_frozen);
int hisi_qm_sriov_configure(struct pci_dev *pdev, int num_vfs);
void hisi_qm_dev_err_init(struct hisi_qm *qm);
void hisi_qm_dev_err_uninit(struct hisi_qm *qm);
int hisi_qm_regs_debugfs_init(struct hisi_qm *qm,
			  struct dfx_diff_registers *dregs, u32 reg_len);
void hisi_qm_regs_debugfs_uninit(struct hisi_qm *qm, u32 reg_len);
void hisi_qm_acc_diff_regs_dump(struct hisi_qm *qm, struct seq_file *s,
				struct dfx_diff_registers *dregs, u32 regs_len);

pci_ers_result_t hisi_qm_dev_err_detected(struct pci_dev *pdev,
					  pci_channel_state_t state);
pci_ers_result_t hisi_qm_dev_slot_reset(struct pci_dev *pdev);
void hisi_qm_reset_prepare(struct pci_dev *pdev);
void hisi_qm_reset_done(struct pci_dev *pdev);

int hisi_qm_wait_mb_ready(struct hisi_qm *qm);
int hisi_qm_mb(struct hisi_qm *qm, u8 cmd, dma_addr_t dma_addr, u16 queue,
	       bool op);

struct hisi_acc_sgl_pool;
struct hisi_acc_hw_sgl *hisi_acc_sg_buf_map_to_hw_sgl(struct device *dev,
	struct scatterlist *sgl, struct hisi_acc_sgl_pool *pool,
	u32 index, dma_addr_t *hw_sgl_dma);
void hisi_acc_sg_buf_unmap(struct device *dev, struct scatterlist *sgl,
			   struct hisi_acc_hw_sgl *hw_sgl);
struct hisi_acc_sgl_pool *hisi_acc_create_sgl_pool(struct device *dev,
						   u32 count, u32 sge_nr);
void hisi_acc_free_sgl_pool(struct device *dev,
			    struct hisi_acc_sgl_pool *pool);
int hisi_qm_alloc_qps_node(struct hisi_qm_list *qm_list, int qp_num,
			   u8 alg_type, int node, struct hisi_qp **qps);
void hisi_qm_free_qps(struct hisi_qp **qps, int qp_num);
void hisi_qm_dev_shutdown(struct pci_dev *pdev);
void hisi_qm_wait_task_finish(struct hisi_qm *qm, struct hisi_qm_list *qm_list);
int hisi_qm_alg_register(struct hisi_qm *qm, struct hisi_qm_list *qm_list, int guard);
void hisi_qm_alg_unregister(struct hisi_qm *qm, struct hisi_qm_list *qm_list, int guard);
int hisi_qm_resume(struct device *dev);
int hisi_qm_suspend(struct device *dev);
void hisi_qm_pm_uninit(struct hisi_qm *qm);
void hisi_qm_pm_init(struct hisi_qm *qm);
int hisi_qm_get_dfx_access(struct hisi_qm *qm);
void hisi_qm_put_dfx_access(struct hisi_qm *qm);
void hisi_qm_regs_dump(struct seq_file *s, struct debugfs_regset32 *regset);
u32 hisi_qm_get_hw_info(struct hisi_qm *qm,
			const struct hisi_qm_cap_info *info_table,
			u32 index, bool is_read);

/* Used by VFIO ACC live migration driver */
struct pci_driver *hisi_sec_get_pf_driver(void);
struct pci_driver *hisi_hpre_get_pf_driver(void);
struct pci_driver *hisi_zip_get_pf_driver(void);
#endif
