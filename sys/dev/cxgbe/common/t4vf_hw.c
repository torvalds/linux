/*-
 * Copyright (c) 2016 Chelsio Communications, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "common.h"
#include "t4_regs.h"
#include "t4_regs_values.h"

#undef msleep
#define msleep(x) do { \
	if (cold) \
		DELAY((x) * 1000); \
	else \
		pause("t4hw", (x) * hz / 1000); \
} while (0)

/*
 * Wait for the device to become ready (signified by our "who am I" register
 * returning a value other than all 1's).  Return an error if it doesn't
 * become ready ...
 */
int t4vf_wait_dev_ready(struct adapter *adapter)
{
	const u32 whoami = VF_PL_REG(A_PL_VF_WHOAMI);
	const u32 notready1 = 0xffffffff;
	const u32 notready2 = 0xeeeeeeee;
	u32 val;

	val = t4_read_reg(adapter, whoami);
	if (val != notready1 && val != notready2)
		return 0;
	msleep(500);
	val = t4_read_reg(adapter, whoami);
	if (val != notready1 && val != notready2)
		return 0;
	else
		return -EIO;
}


/**
 *      t4vf_fw_reset - issue a reset to FW
 *      @adapter: the adapter
 *
 *	Issues a reset command to FW.  For a Physical Function this would
 *	result in the Firmware reseting all of its state.  For a Virtual
 *	Function this just resets the state associated with the VF.
 */
int t4vf_fw_reset(struct adapter *adapter)
{
	struct fw_reset_cmd cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.op_to_write = cpu_to_be32(V_FW_CMD_OP(FW_RESET_CMD) |
				      F_FW_CMD_WRITE);
	cmd.retval_len16 = cpu_to_be32(V_FW_CMD_LEN16(FW_LEN16(cmd)));
	return t4vf_wr_mbox(adapter, &cmd, sizeof(cmd), NULL);
}

/**
 *	t4vf_get_sge_params - retrieve adapter Scatter gather Engine parameters
 *	@adapter: the adapter
 *
 *	Retrieves various core SGE parameters in the form of hardware SGE
 *	register values.  The caller is responsible for decoding these as
 *	needed.  The SGE parameters are stored in @adapter->params.sge.
 */
int t4vf_get_sge_params(struct adapter *adapter)
{
	struct sge_params *sp = &adapter->params.sge;
	u32 params[7], vals[7];
	u32 whoami;
	unsigned int pf, s_hps;
	int i, v;

	params[0] = (V_FW_PARAMS_MNEM(FW_PARAMS_MNEM_REG) |
		     V_FW_PARAMS_PARAM_XYZ(A_SGE_CONTROL));
	params[1] = (V_FW_PARAMS_MNEM(FW_PARAMS_MNEM_REG) |
		     V_FW_PARAMS_PARAM_XYZ(A_SGE_HOST_PAGE_SIZE));
	params[2] = (V_FW_PARAMS_MNEM(FW_PARAMS_MNEM_REG) |
		     V_FW_PARAMS_PARAM_XYZ(A_SGE_TIMER_VALUE_0_AND_1));
	params[3] = (V_FW_PARAMS_MNEM(FW_PARAMS_MNEM_REG) |
		     V_FW_PARAMS_PARAM_XYZ(A_SGE_TIMER_VALUE_2_AND_3));
	params[4] = (V_FW_PARAMS_MNEM(FW_PARAMS_MNEM_REG) |
		     V_FW_PARAMS_PARAM_XYZ(A_SGE_TIMER_VALUE_4_AND_5));
	params[5] = (V_FW_PARAMS_MNEM(FW_PARAMS_MNEM_REG) |
		     V_FW_PARAMS_PARAM_XYZ(A_SGE_CONM_CTRL));
	params[6] = (V_FW_PARAMS_MNEM(FW_PARAMS_MNEM_REG) |
		     V_FW_PARAMS_PARAM_XYZ(A_SGE_INGRESS_RX_THRESHOLD));
	v = t4vf_query_params(adapter, 7, params, vals);
	if (v != FW_SUCCESS)
		return v;

	sp->sge_control = vals[0];
	sp->counter_val[0] = G_THRESHOLD_0(vals[6]);
	sp->counter_val[1] = G_THRESHOLD_1(vals[6]);
	sp->counter_val[2] = G_THRESHOLD_2(vals[6]);
	sp->counter_val[3] = G_THRESHOLD_3(vals[6]);
	sp->timer_val[0] = core_ticks_to_us(adapter, G_TIMERVALUE0(vals[2]));
	sp->timer_val[1] = core_ticks_to_us(adapter, G_TIMERVALUE1(vals[2]));
	sp->timer_val[2] = core_ticks_to_us(adapter, G_TIMERVALUE2(vals[3]));
	sp->timer_val[3] = core_ticks_to_us(adapter, G_TIMERVALUE3(vals[3]));
	sp->timer_val[4] = core_ticks_to_us(adapter, G_TIMERVALUE4(vals[4]));
	sp->timer_val[5] = core_ticks_to_us(adapter, G_TIMERVALUE5(vals[4]));

	sp->fl_starve_threshold = G_EGRTHRESHOLD(vals[5]) * 2 + 1;
	if (is_t4(adapter))
		sp->fl_starve_threshold2 = sp->fl_starve_threshold;
	else if (is_t5(adapter))
		sp->fl_starve_threshold2 = G_EGRTHRESHOLDPACKING(vals[5]) * 2 + 1;
	else
		sp->fl_starve_threshold2 = G_T6_EGRTHRESHOLDPACKING(vals[5]) * 2 + 1;

	/*
	 * We need the Queues/Page and Host Page Size for our VF.
	 * This is based on the PF from which we're instantiated.
	 */
	whoami = t4_read_reg(adapter, VF_PL_REG(A_PL_VF_WHOAMI));
	pf = G_SOURCEPF(whoami);

	s_hps = (S_HOSTPAGESIZEPF0 +
	    (S_HOSTPAGESIZEPF1 - S_HOSTPAGESIZEPF0) * pf);
	sp->page_shift = ((vals[1] >> s_hps) & M_HOSTPAGESIZEPF0) + 10;

	for (i = 0; i < SGE_FLBUF_SIZES; i++) {
		params[0] = (V_FW_PARAMS_MNEM(FW_PARAMS_MNEM_REG) |
		    V_FW_PARAMS_PARAM_XYZ(A_SGE_FL_BUFFER_SIZE0 + (4 * i)));
		v = t4vf_query_params(adapter, 1, params, vals);
		if (v != FW_SUCCESS)
			return v;

		sp->sge_fl_buffer_size[i] = vals[0];
	}

	/*
	 * T4 uses a single control field to specify both the PCIe Padding and
	 * Packing Boundary.  T5 introduced the ability to specify these
	 * separately with the Padding Boundary in SGE_CONTROL and and Packing
	 * Boundary in SGE_CONTROL2.  So for T5 and later we need to grab
	 * SGE_CONTROL in order to determine how ingress packet data will be
	 * laid out in Packed Buffer Mode.  Unfortunately, older versions of
	 * the firmware won't let us retrieve SGE_CONTROL2 so if we get a
	 * failure grabbing it we throw an error since we can't figure out the
	 * right value.
	 */
	sp->spg_len = sp->sge_control & F_EGRSTATUSPAGESIZE ? 128 : 64;
	sp->fl_pktshift = G_PKTSHIFT(sp->sge_control);
	if (chip_id(adapter) <= CHELSIO_T5) {
		sp->pad_boundary = 1 << (G_INGPADBOUNDARY(sp->sge_control) +
		    X_INGPADBOUNDARY_SHIFT);
	} else {
		sp->pad_boundary = 1 << (G_INGPADBOUNDARY(sp->sge_control) +
		    X_T6_INGPADBOUNDARY_SHIFT);
	}
	if (is_t4(adapter))
		sp->pack_boundary = sp->pad_boundary;
	else {
		params[0] = (V_FW_PARAMS_MNEM(FW_PARAMS_MNEM_REG) |
			     V_FW_PARAMS_PARAM_XYZ(A_SGE_CONTROL2));
		v = t4vf_query_params(adapter, 1, params, vals);
		if (v != FW_SUCCESS) {
			CH_ERR(adapter, "Unable to get SGE Control2; "
			       "probably old firmware.\n");
			return v;
		}
		if (G_INGPACKBOUNDARY(vals[0]) == 0)
			sp->pack_boundary = 16;
		else
			sp->pack_boundary = 1 << (G_INGPACKBOUNDARY(vals[0]) +
			    5);
	}

	/*
	 * For T5 and later we want to use the new BAR2 Doorbells.
	 * Unfortunately, older firmware didn't allow the this register to be
	 * read.
	 */
	if (!is_t4(adapter)) {
		unsigned int s_qpp;

		params[0] = (V_FW_PARAMS_MNEM(FW_PARAMS_MNEM_REG) |
			     V_FW_PARAMS_PARAM_XYZ(A_SGE_EGRESS_QUEUES_PER_PAGE_VF));
		params[1] = (V_FW_PARAMS_MNEM(FW_PARAMS_MNEM_REG) |
			     V_FW_PARAMS_PARAM_XYZ(A_SGE_INGRESS_QUEUES_PER_PAGE_VF));
		v = t4vf_query_params(adapter, 2, params, vals);
		if (v != FW_SUCCESS) {
			CH_WARN(adapter, "Unable to get VF SGE Queues/Page; "
				"probably old firmware.\n");
			return v;
		}

		s_qpp = (S_QUEUESPERPAGEPF0 +
			 (S_QUEUESPERPAGEPF1 - S_QUEUESPERPAGEPF0) * pf);
		sp->eq_s_qpp = ((vals[0] >> s_qpp) & M_QUEUESPERPAGEPF0);
		sp->iq_s_qpp = ((vals[1] >> s_qpp) & M_QUEUESPERPAGEPF0);
	}

	return 0;
}

/**
 *	t4vf_get_rss_glb_config - retrieve adapter RSS Global Configuration
 *	@adapter: the adapter
 *
 *	Retrieves global RSS mode and parameters with which we have to live
 *	and stores them in the @adapter's RSS parameters.
 */
int t4vf_get_rss_glb_config(struct adapter *adapter)
{
	struct rss_params *rss = &adapter->params.rss;
	struct fw_rss_glb_config_cmd cmd, rpl;
	int v;

	/*
	 * Execute an RSS Global Configuration read command to retrieve
	 * our RSS configuration.
	 */
	memset(&cmd, 0, sizeof(cmd));
	cmd.op_to_write = cpu_to_be32(V_FW_CMD_OP(FW_RSS_GLB_CONFIG_CMD) |
				      F_FW_CMD_REQUEST |
				      F_FW_CMD_READ);
	cmd.retval_len16 = cpu_to_be32(FW_LEN16(cmd));
	v = t4vf_wr_mbox(adapter, &cmd, sizeof(cmd), &rpl);
	if (v != FW_SUCCESS)
		return v;

	/*
	 * Transate the big-endian RSS Global Configuration into our
	 * cpu-endian format based on the RSS mode.  We also do first level
	 * filtering at this point to weed out modes which don't support
	 * VF Drivers ...
	 */
	rss->mode = G_FW_RSS_GLB_CONFIG_CMD_MODE(
			be32_to_cpu(rpl.u.manual.mode_pkd));
	switch (rss->mode) {
	case FW_RSS_GLB_CONFIG_CMD_MODE_BASICVIRTUAL: {
		u32 word = be32_to_cpu(
				rpl.u.basicvirtual.synmapen_to_hashtoeplitz);

		rss->u.basicvirtual.synmapen =
			((word & F_FW_RSS_GLB_CONFIG_CMD_SYNMAPEN) != 0);
		rss->u.basicvirtual.syn4tupenipv6 =
			((word & F_FW_RSS_GLB_CONFIG_CMD_SYN4TUPENIPV6) != 0);
		rss->u.basicvirtual.syn2tupenipv6 =
			((word & F_FW_RSS_GLB_CONFIG_CMD_SYN2TUPENIPV6) != 0);
		rss->u.basicvirtual.syn4tupenipv4 =
			((word & F_FW_RSS_GLB_CONFIG_CMD_SYN4TUPENIPV4) != 0);
		rss->u.basicvirtual.syn2tupenipv4 =
			((word & F_FW_RSS_GLB_CONFIG_CMD_SYN2TUPENIPV4) != 0);

		rss->u.basicvirtual.ofdmapen =
			((word & F_FW_RSS_GLB_CONFIG_CMD_OFDMAPEN) != 0);

		rss->u.basicvirtual.tnlmapen =
			((word & F_FW_RSS_GLB_CONFIG_CMD_TNLMAPEN) != 0);
		rss->u.basicvirtual.tnlalllookup =
			((word  & F_FW_RSS_GLB_CONFIG_CMD_TNLALLLKP) != 0);

		rss->u.basicvirtual.hashtoeplitz =
			((word & F_FW_RSS_GLB_CONFIG_CMD_HASHTOEPLITZ) != 0);

		/* we need at least Tunnel Map Enable to be set */
		if (!rss->u.basicvirtual.tnlmapen)
			return -EINVAL;
		break;
	}

	default:
		/* all unknown/unsupported RSS modes result in an error */
		return -EINVAL;
	}

	return 0;
}

/**
 *	t4vf_get_vfres - retrieve VF resource limits
 *	@adapter: the adapter
 *
 *	Retrieves configured resource limits and capabilities for a virtual
 *	function.  The results are stored in @adapter->vfres.
 */
int t4vf_get_vfres(struct adapter *adapter)
{
	struct vf_resources *vfres = &adapter->params.vfres;
	struct fw_pfvf_cmd cmd, rpl;
	int v;
	u32 word;

	/*
	 * Execute PFVF Read command to get VF resource limits; bail out early
	 * with error on command failure.
	 */
	memset(&cmd, 0, sizeof(cmd));
	cmd.op_to_vfn = cpu_to_be32(V_FW_CMD_OP(FW_PFVF_CMD) |
				    F_FW_CMD_REQUEST |
				    F_FW_CMD_READ);
	cmd.retval_len16 = cpu_to_be32(FW_LEN16(cmd));
	v = t4vf_wr_mbox(adapter, &cmd, sizeof(cmd), &rpl);
	if (v != FW_SUCCESS)
		return v;

	/*
	 * Extract VF resource limits and return success.
	 */
	word = be32_to_cpu(rpl.niqflint_niq);
	vfres->niqflint = G_FW_PFVF_CMD_NIQFLINT(word);
	vfres->niq = G_FW_PFVF_CMD_NIQ(word);

	word = be32_to_cpu(rpl.type_to_neq);
	vfres->neq = G_FW_PFVF_CMD_NEQ(word);
	vfres->pmask = G_FW_PFVF_CMD_PMASK(word);

	word = be32_to_cpu(rpl.tc_to_nexactf);
	vfres->tc = G_FW_PFVF_CMD_TC(word);
	vfres->nvi = G_FW_PFVF_CMD_NVI(word);
	vfres->nexactf = G_FW_PFVF_CMD_NEXACTF(word);

	word = be32_to_cpu(rpl.r_caps_to_nethctrl);
	vfres->r_caps = G_FW_PFVF_CMD_R_CAPS(word);
	vfres->wx_caps = G_FW_PFVF_CMD_WX_CAPS(word);
	vfres->nethctrl = G_FW_PFVF_CMD_NETHCTRL(word);

	return 0;
}

/**
 */
int t4vf_prep_adapter(struct adapter *adapter)
{
	int err;

	/*
	 * Wait for the device to become ready before proceeding ...
	 */
	err = t4vf_wait_dev_ready(adapter);
	if (err)
		return err;

	adapter->params.chipid = pci_get_device(adapter->dev) >> 12;
	if (adapter->params.chipid >= 0xa) {
		adapter->params.chipid -= (0xa - 0x4);
		adapter->params.fpga = 1;
	}
	
	/*
	 * Default port and clock for debugging in case we can't reach
	 * firmware.
	 */
	adapter->params.nports = 1;
	adapter->params.vfres.pmask = 1;
	adapter->params.vpd.cclk = 50000;

	adapter->chip_params = t4_get_chip_params(chip_id(adapter));
	if (adapter->chip_params == NULL)
		return -EINVAL;

	return 0;
}
