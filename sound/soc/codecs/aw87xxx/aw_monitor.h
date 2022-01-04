/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef __AW_MONITOR_H__
#define __AW_MONITOR_H__

#define AW_WAIT_DSP_OPEN_TIME			(3000)
#define AW_VBAT_CAPACITY_MIN			(0)
#define AW_VBAT_CAPACITY_MAX			(100)
#define AW_VMAX_INIT_VAL			(0xFFFFFFFF)
#define AW_VBAT_MAX				(100)
#define AW_VMAX_MAX				(0)
#define AW_DEFAULT_MONITOR_TIME			(3000)
#define AW_WAIT_TIME				(3000)
#define REG_STATUS_CHECK_MAX			(10)
#define AW_ESD_CHECK_DELAY			(1)

#define AW_ESD_ENABLE				(true)
#define AW_ESD_DISABLE				(false)

enum aw_monitor_init {
	AW_MONITOR_CFG_WAIT = 0,
	AW_MONITOR_CFG_OK = 1,
};

enum aw_monitor_hdr_info {
	AW_MONITOR_HDR_DATA_SIZE = 0x00000004,
	AW_MONITOR_HDR_DATA_BYTE_LEN = 0x00000004,
};

enum aw_monitor_data_ver {
	AW_MONITOR_DATA_VER = 0x00000001,
	AW_MONITOR_DATA_VER_MAX,
};

enum aw_monitor_first_enter {
	AW_FIRST_ENTRY = 0,
	AW_NOT_FIRST_ENTRY = 1,
};

struct aw_bin_header {
	uint32_t check_sum;
	uint32_t header_ver;
	uint32_t bin_data_type;
	uint32_t bin_data_ver;
	uint32_t bin_data_size;
	uint32_t ui_ver;
	char product[8];
	uint32_t addr_byte_len;
	uint32_t data_byte_len;
	uint32_t device_addr;
	uint32_t reserve[4];
};

struct aw_monitor_header {
	uint32_t monitor_switch;
	uint32_t monitor_time;
	uint32_t monitor_count;
	uint32_t step_count;
	uint32_t reserve[4];
};

struct vmax_step_config {
	uint32_t vbat_min;
	uint32_t vbat_max;
	int vmax_vol;
};

struct aw_monitor {
	bool open_dsp_en;
	bool esd_enable;
	int32_t dev_index;
	uint8_t first_entry;
	uint8_t timer_cnt;
	uint32_t vbat_sum;
	uint32_t custom_capacity;
	uint32_t pre_vmax;

	int bin_status;
	struct aw_monitor_header monitor_hdr;
	struct vmax_step_config *vmax_cfg;

	struct delayed_work with_dsp_work;
};

void aw_monitor_cfg_free(struct aw_monitor *monitor);
int aw_monitor_bin_parse(struct device *dev,
			char *monitor_data, uint32_t data_len);
void aw_monitor_stop(struct aw_monitor *monitor);
void aw_monitor_start(struct aw_monitor *monitor);
int aw_monitor_no_dsp_get_vmax(struct aw_monitor *monitor,
					int32_t *vmax);
void aw_monitor_init(struct device *dev, struct aw_monitor *monitor,
				struct device_node *dev_node);
void aw_monitor_exit(struct aw_monitor *monitor);
#endif
