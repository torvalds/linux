/*-
 * Copyright (c) 2013-2017, Mellanox Technologies, Ltd.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS `AS IS' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <dev/mlx5/driver.h>
#include <dev/mlx5/diagnostics.h>

const struct mlx5_core_diagnostics_entry
	mlx5_core_pci_diagnostics_table[
		MLX5_CORE_PCI_DIAGNOSTICS_NUM] = {
	MLX5_CORE_PCI_DIAGNOSTICS(MLX5_CORE_DIAGNOSTICS_ENTRY)
};

const struct mlx5_core_diagnostics_entry
	mlx5_core_general_diagnostics_table[
		MLX5_CORE_GENERAL_DIAGNOSTICS_NUM] = {
	MLX5_CORE_GENERAL_DIAGNOSTICS(MLX5_CORE_DIAGNOSTICS_ENTRY)
};

static int mlx5_core_get_index_of_diag_counter(
	const struct mlx5_core_diagnostics_entry *entry,
	int size, u16 counter_id)
{
	int x;

	/* check for invalid counter ID */
	if (counter_id == 0)
		return -1;

	/* lookup counter ID in table */
	for (x = 0; x != size; x++) {
		if (entry[x].counter_id == counter_id)
			return x;
	}
	return -1;
}

static void mlx5_core_put_diag_counter(
	const struct mlx5_core_diagnostics_entry *entry,
	u64 *array, int size, u16 counter_id, u64 value)
{
	int x;

	/* check for invalid counter ID */
	if (counter_id == 0)
		return;

	/* lookup counter ID in table */
	for (x = 0; x != size; x++) {
		if (entry[x].counter_id == counter_id) {
			array[x] = value;
			break;
		}
	}
}

int mlx5_core_set_diagnostics_full(struct mlx5_core_dev *dev,
				   u8 enable_pci, u8 enable_general)
{
	void *diag_params_ctx;
	void *in;
	int numcounters;
	int inlen;
	int err;
	int x;
	int y;

	if (MLX5_CAP_GEN(dev, debug) == 0)
		return 0;

	numcounters = MLX5_CAP_GEN(dev, num_of_diagnostic_counters);
	if (numcounters == 0)
		return 0;

	inlen = MLX5_ST_SZ_BYTES(set_diagnostic_params_in) +
	    MLX5_ST_SZ_BYTES(diagnostic_counter) * numcounters;
	in = mlx5_vzalloc(inlen);
	if (in == NULL)
		return -ENOMEM;

	diag_params_ctx = MLX5_ADDR_OF(set_diagnostic_params_in, in,
				       diagnostic_params_ctx);

	MLX5_SET(diagnostic_params_context, diag_params_ctx,
		 enable, enable_pci || enable_general);
	MLX5_SET(diagnostic_params_context, diag_params_ctx,
		 single, 1);
	MLX5_SET(diagnostic_params_context, diag_params_ctx,
		 on_demand, 1);

	/* collect the counters we want to enable */
	for (x = y = 0; x != numcounters; x++) {
		u16 counter_id =
			MLX5_CAP_DEBUG(dev, diagnostic_counter[x].counter_id);
		int index = -1;

		if (index < 0 && enable_pci != 0) {
			/* check if counter ID exists in local table */
			index = mlx5_core_get_index_of_diag_counter(
			    mlx5_core_pci_diagnostics_table,
			    MLX5_CORE_PCI_DIAGNOSTICS_NUM,
			    counter_id);
		}
		if (index < 0 && enable_general != 0) {
			/* check if counter ID exists in local table */
			index = mlx5_core_get_index_of_diag_counter(
			    mlx5_core_general_diagnostics_table,
			    MLX5_CORE_GENERAL_DIAGNOSTICS_NUM,
			    counter_id);
		}
		if (index < 0)
			continue;

		MLX5_SET(diagnostic_params_context,
			 diag_params_ctx,
			 counter_id[y].counter_id,
			 counter_id);
		y++;
	}

	/* recompute input length */
	inlen = MLX5_ST_SZ_BYTES(set_diagnostic_params_in) +
	    MLX5_ST_SZ_BYTES(diagnostic_counter) * y;

	/* set number of counters */
	MLX5_SET(diagnostic_params_context, diag_params_ctx,
		 num_of_counters, y);

	/* execute firmware command */
	err = mlx5_set_diagnostic_params(dev, in, inlen);

	kvfree(in);

	return err;
}

int mlx5_core_get_diagnostics_full(struct mlx5_core_dev *dev,
				   union mlx5_core_pci_diagnostics *pdiag,
				   union mlx5_core_general_diagnostics *pgen)
{
	void *out;
	void *in;
	int numcounters;
	int outlen;
	int inlen;
	int err;
	int x;

	if (MLX5_CAP_GEN(dev, debug) == 0)
		return 0;

	numcounters = MLX5_CAP_GEN(dev, num_of_diagnostic_counters);
	if (numcounters == 0)
		return 0;

	outlen = MLX5_ST_SZ_BYTES(query_diagnostic_counters_out) +
	    MLX5_ST_SZ_BYTES(diagnostic_counter) * numcounters;

	out = mlx5_vzalloc(outlen);
	if (out == NULL)
		return -ENOMEM;

	err = mlx5_query_diagnostic_counters(dev, 1, 0, out, outlen);
	if (err == 0) {
		for (x = 0; x != numcounters; x++) {
			u16 counter_id = MLX5_GET(
			    query_diagnostic_counters_out,
			    out, diag_counter[x].counter_id);
			u64 counter_value = MLX5_GET64(
			    query_diagnostic_counters_out,
			    out, diag_counter[x].counter_value_h);

			if (pdiag != NULL) {
				mlx5_core_put_diag_counter(
				    mlx5_core_pci_diagnostics_table,
				    pdiag->array,
				    MLX5_CORE_PCI_DIAGNOSTICS_NUM,
				    counter_id, counter_value);
			}
			if (pgen != NULL) {
				mlx5_core_put_diag_counter(
				    mlx5_core_general_diagnostics_table,
				    pgen->array,
				    MLX5_CORE_GENERAL_DIAGNOSTICS_NUM,
				    counter_id, counter_value);
			}
		}
	}
	kvfree(out);

	if (pdiag != NULL) {
		inlen = MLX5_ST_SZ_BYTES(mpcnt_reg);
		outlen = MLX5_ST_SZ_BYTES(mpcnt_reg);

		in = mlx5_vzalloc(inlen);
		if (in == NULL)
			return -ENOMEM;

		out = mlx5_vzalloc(outlen);
		if (out == NULL) {
			kvfree(in);
			return -ENOMEM;
		}
		MLX5_SET(mpcnt_reg, in, grp,
			 MLX5_PCIE_PERFORMANCE_COUNTERS_GROUP);

		err = mlx5_core_access_reg(dev, in, inlen, out, outlen,
					   MLX5_REG_MPCNT, 0, 0);
		if (err == 0) {
			void *pcounters = MLX5_ADDR_OF(mpcnt_reg, out,
			    counter_set.pcie_performance_counters_data_layout);

			pdiag->counter.rx_pci_errors =
			    MLX5_GET(pcie_performance_counters_data_layout,
				     pcounters, rx_errors);
			pdiag->counter.tx_pci_errors =
			    MLX5_GET(pcie_performance_counters_data_layout,
				     pcounters, tx_errors);
		}
		MLX5_SET(mpcnt_reg, in, grp,
			 MLX5_PCIE_TIMERS_AND_STATES_COUNTERS_GROUP);

		err = mlx5_core_access_reg(dev, in, inlen, out, outlen,
		    MLX5_REG_MPCNT, 0, 0);
		if (err == 0) {
			void *pcounters = MLX5_ADDR_OF(mpcnt_reg, out,
			    counter_set.pcie_timers_and_states_data_layout);

			pdiag->counter.tx_pci_non_fatal_errors =
			    MLX5_GET(pcie_timers_and_states_data_layout,
				     pcounters, non_fatal_err_msg_sent);
			pdiag->counter.tx_pci_fatal_errors =
			    MLX5_GET(pcie_timers_and_states_data_layout,
				     pcounters, fatal_err_msg_sent);
		}
		kvfree(in);
		kvfree(out);
	}
	return 0;
}

int mlx5_core_supports_diagnostics(struct mlx5_core_dev *dev, u16 counter_id)
{
	int numcounters;
	int x;

	if (MLX5_CAP_GEN(dev, debug) == 0)
		return 0;

	/* check for any counter */
	if (counter_id == 0)
		return 1;

	numcounters = MLX5_CAP_GEN(dev, num_of_diagnostic_counters);

	/* check if counter ID exists in debug capability */
	for (x = 0; x != numcounters; x++) {
		if (MLX5_CAP_DEBUG(dev, diagnostic_counter[x].counter_id) ==
		    counter_id)
			return 1;
	}
	return 0;			/* not supported counter */
}
