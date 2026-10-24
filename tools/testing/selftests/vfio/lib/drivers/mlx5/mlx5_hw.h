/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * mlx5 VFIO selftest driver - HW definitions
 *
 * Typed wrappers, constants, and helpers for programming mlx5 hardware
 * via the VFIO selftest framework.  Most HW constants and all MLX5_SET/GET
 * macros come from the kernel headers (mlx5_ifc.h, mlx5_ifc_macros.h).
 */
#ifndef SELFTESTS_VFIO_MLX5_HW_H
#define SELFTESTS_VFIO_MLX5_HW_H

#include <linux/io.h>
#include <linux/build_bug.h>
#include <vdso/bits.h>

#include "mlx5_ifc.h"
#include "mlx5_ifc_macros.h"

/*
 * Typed HW object wrappers for driver region arrays.
 *
 * The IFC _bits structs have sizeof == num_bits (not bytes), so they cannot
 * be used as array elements.  These wrappers provide byte-sized types.
 */
#define MLX5ST_MAKE_DATA32(name)               \
	struct mlx5st_##name {                 \
		u32 data[MLX5_ST_SZ_DW(name)]; \
	}
#define MLX5ST_MAKE_DATA64(name)               \
	struct mlx5st_##name {                 \
		u64 data[MLX5_ST_SZ_QW(name)]; \
	}

MLX5ST_MAKE_DATA32(initial_seg);
MLX5ST_MAKE_DATA64(cmd_queue_entry);
MLX5ST_MAKE_DATA64(cmd_if_box);
MLX5ST_MAKE_DATA64(wqe_ctrl_seg);
MLX5ST_MAKE_DATA64(wqe_raddr_seg);
MLX5ST_MAKE_DATA64(wqe_data_seg);
MLX5ST_MAKE_DATA64(cqe64) __aligned(64);
MLX5ST_MAKE_DATA64(eqe);

/*
 * Mailbox blocks: 512 data + 64 header = 576 bytes, but the
 * next_pointer field stores bits [31:10], requiring 1024-byte alignment.
 */
#define CMD_MBOX_SIZE (2 * MLX5_HW_PAGE_SIZE)
#define CMD_MBOX_STRIDE 1024
#define CMD_MBOX_NENT (CMD_MBOX_SIZE / CMD_MBOX_STRIDE)
/* Stride-aligned mailbox entry — block + padding to 1024 bytes */
struct mlx5st_mbox_entry {
	struct mlx5st_cmd_if_box block;
} __aligned(CMD_MBOX_STRIDE);

#define MLX5_CMD_INLINE_SZ \
	MLX5_FLD_SZ_BYTES(cmd_queue_entry, command_input_inline_data)

/* Command interface mailbox block (512 data + 64 header) */
#define MLX5_CMD_DATA_BLOCK_SIZE MLX5_FLD_SZ_BYTES(cmd_if_box, mailbox_data)

/* RDMA Write WQE — one basic block: ctrl + raddr + data + padding */
struct mlx5st_send_wqe {
	struct mlx5st_wqe_ctrl_seg ctrl;
	struct mlx5st_wqe_raddr_seg raddr;
	struct mlx5st_wqe_data_seg data;
} __aligned(64);
static_assert(sizeof(struct mlx5st_send_wqe) == 64,
	      "send WQE segments must fit in one BB");

/* DS = number of 16-byte segments in the WQE (ctrl + raddr + data) */
#define MLX5_RDMA_WRITE_DS 3

/* Doorbell record — two __be32 in a 64-byte aligned pair */
struct mlx5st_dbrec {
	__be32 recv_counter;
	__be32 send_counter;
} __aligned(64);

/* UAR BlueFlame buffer offsets within a UAR page */
#define MLX5_BF_OFFSET 0x800
#define MLX5_BF_SIZE 0x100

/* CQ doorbell offset within UAR page */
#define MLX5_CQ_DOORBELL_OFFSET 0x20

/* EQ doorbell offset within UAR page */
#define MLX5_EQ_DOORBELL_OFFSET 0x40

#define MLX5_HW_PAGE_SIZE 4096

/*
 * Test parameters
 */
#define SQ_WQE_CNT 16
#define LOG_SQ_SIZE 4
#define CQ_CQE_CNT 16
#define LOG_CQ_SIZE 4
#define EQ_NENT 64
#define LOG_EQ_SIZE 6
#define MSI_EQ_NENT 16
#define LOG_MSI_EQ_SIZE 4
#define MSI_VECTOR 0

#define MAX_FW_PAGES 8192
#define MAX_FW_PAGES_PER_CMD 512

#define MLX5_CMD_TIMEOUT_MS 5000

static inline u32 mlx5st_idx_to_mkey(u32 mkey_idx)
{
	return mkey_idx << 8;
}

#endif /* SELFTESTS_VFIO_MLX5_HW_H */
