/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef __AWINIC_CALIBRATION_H__
#define __AWINIC_CALIBRATION_H__

/*#define AW_CALI_STORE_EXAMPLE*/

#define AW_CALI_STORE_EXAMPLE
#define AW_ERRO_CALI_RE_VALUE (0)
#define AW_ERRO_CALI_F0_VALUE (2600)

#define AW_CALI_RE_DEFAULT_TIMER (3000)
#define MSGS_SIZE (512)
#define RESERVED_SIZE (252)


#define AW_CALI_ALL_DEV (0xFFFFFFFF)

#define AW_CALI_RE_MAX (15000)
#define AW_CALI_RE_MIN (4000)
#define AW_CALI_CFG_NUM (4)
#define AW_CALI_F0_DATA_NUM (4)
#define AW_CALI_READ_CNT_MAX (8)
#define AW_CALI_DATA_SUM_RM (2)
#define AW_DSP_RE_TO_SHOW_RE(re, shift) (((re) * (1000)) >> (shift))
#define AW_SHOW_RE_TO_DSP_RE(re, shift)  (((re) << shift) / (1000))
#define AW_CALI_F0_TIME (5 * 1000)
#define F0_READ_CNT_MAX (5)
#define AW_FS_CFG_MAX	(11)
#define AW_DEV_CH_MAX	(16)
#define AW_DEV_RE_RANGE	(RE_RANGE_NUM * AW_DEV_CH_MAX)
#define AW_TE_CACL_VALUE(te, coil_alpha) ((int32_t)(((int32_t)te << 18) / (coil_alpha)))
#define AW_RE_REALTIME_VALUE(re_cacl, te_cacl) ((re_cacl) + (int32_t)((int64_t)((te_cacl) * (re_cacl)) >> 14))

enum {
	CALI_CHECK_DISABLE = 0,
	CALI_CHECK_ENABLE = 1,
};

enum {
	CALI_RESULT_NONE = 0,
	CALI_RESULT_NORMAL = 1,
	CALI_RESULT_ERROR = -1,
};

enum {
	CALI_OPS_HMUTE = 0X0001,
	CALI_OPS_NOISE = 0X0002,
};

enum {
	CALI_TYPE_RE = 0,
	CALI_TYPE_F0,
};

enum{
	AW_CALI_MODE_NONE = 0,
	AW_CALI_MODE_ATTR,
	AW_CALI_MODE_CLASS,
	AW_CALI_MODE_MISC,
	AW_CALI_MODE_MAX
};

enum {
	GET_RE_TYPE = 0,
	GET_F0_TYPE,
	GET_Q_TYPE,
};

enum {
	AW_CALI_CMD_RE = 0,
	AW_CALI_CMD_F0,
	AW_CALI_CMD_RE_F0,
	AW_CALI_CMD_F0_Q,
	AW_CALI_CMD_RE_F0_Q,
};

enum {
	CALI_STR_NONE = 0,
	CALI_STR_CALI_RE_F0,
	CALI_STR_CALI_RE,
	CALI_STR_CALI_F0,
	CALI_STR_SET_RE,
	CALI_STR_SHOW_RE,		/*show cali_re*/
	CALI_STR_SHOW_R0,		/*show real r0*/
	CALI_STR_SHOW_CALI_F0,		/*GET DEV CALI_F0*/
	CALI_STR_SHOW_F0,		/*SHOW REAL F0*/
	CALI_STR_SHOW_TE,
	CALI_STR_DEV_SEL,		/*switch device*/
	CALI_STR_VER,
	CALI_STR_SHOW_RE_RANGE,
	CALI_STR_MAX,
};

enum {
	RE_MIN_FLAG = 0,
	RE_MAX_FLAG = 1,
	RE_RANGE_NUM = 2,
};

struct re_data {
	uint32_t re_range[2];
};


#define AW_IOCTL_MAGIC				'a'

#define AW_IOCTL_GET_F0				_IOWR(AW_IOCTL_MAGIC, 5, int32_t)
#define AW_IOCTL_SET_CALI_RE			_IOWR(AW_IOCTL_MAGIC, 6, int32_t)

#define AW_IOCTL_GET_RE				_IOWR(AW_IOCTL_MAGIC, 17, int32_t)
#define AW_IOCTL_GET_CALI_F0			_IOWR(AW_IOCTL_MAGIC, 18, int32_t)
#define AW_IOCTL_GET_REAL_R0			_IOWR(AW_IOCTL_MAGIC, 19, int32_t)
#define AW_IOCTL_GET_TE				_IOWR(AW_IOCTL_MAGIC, 20, int32_t)
#define AW_IOCTL_GET_RE_RANGE			_IOWR(AW_IOCTL_MAGIC, 21, struct re_data)

struct cali_cfg {
	uint32_t data[AW_CALI_CFG_NUM];
};


struct aw_cali_desc {
	bool status;
	struct cali_cfg cali_cfg;
	uint16_t store_vol;
	uint32_t cali_re;	/*cali value*/
	uint32_t f0;
	uint32_t q;
	uint32_t ra;
	int8_t cali_result;
	uint8_t cali_check_st;
};

void aw_cali_init(struct aw_cali_desc *cali_desc);
void aw_cali_deinit(struct aw_cali_desc *cali_desc);
bool aw_cali_svc_get_cali_status(struct aw_cali_desc *cali_desc);
int aw_cali_svc_set_cali_re_to_dsp(struct aw_cali_desc *cali_desc);
int aw_cali_svc_get_ra(struct aw_cali_desc *cali_desc);
int aw_cali_svc_get_dev_te(struct aw_cali_desc *cali_desc, int32_t *te);
int aw_cali_get_cali_re(struct aw_cali_desc *cali_desc);
int aw_cali_read_cali_re_from_dsp(struct aw_cali_desc *cali_desc, uint32_t *re);



#endif


