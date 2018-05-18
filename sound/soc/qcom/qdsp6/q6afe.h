/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __Q6AFE_H__
#define __Q6AFE_H__

#include <dt-bindings/sound/qcom,q6afe.h>

#define AFE_PORT_MAX		48

#define MSM_AFE_PORT_TYPE_RX 0
#define MSM_AFE_PORT_TYPE_TX 1
#define AFE_MAX_PORTS AFE_PORT_MAX

#define AFE_MAX_CHAN_COUNT	8
#define AFE_PORT_MAX_AUDIO_CHAN_CNT	0x8

struct q6afe_hdmi_cfg {
	u16                  datatype;
	u16                  channel_allocation;
	u32                  sample_rate;
	u16                  bit_width;
};

struct q6afe_slim_cfg {
	u32	sample_rate;
	u16	bit_width;
	u16	data_format;
	u16	num_channels;
	u8	ch_mapping[AFE_MAX_CHAN_COUNT];
};

struct q6afe_port_config {
	struct q6afe_hdmi_cfg hdmi;
	struct q6afe_slim_cfg slim;
};

struct q6afe_port;

struct q6afe_port *q6afe_port_get_from_id(struct device *dev, int id);
int q6afe_port_start(struct q6afe_port *port);
int q6afe_port_stop(struct q6afe_port *port);
void q6afe_port_put(struct q6afe_port *port);
int q6afe_get_port_id(int index);
void q6afe_hdmi_port_prepare(struct q6afe_port *port,
			    struct q6afe_hdmi_cfg *cfg);
void q6afe_slim_port_prepare(struct q6afe_port *port,
			  struct q6afe_slim_cfg *cfg);

#endif /* __Q6AFE_H__ */
