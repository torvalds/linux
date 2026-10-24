/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2013-2026, Mellanox Technologies. All rights reserved.
 *
 * Accessor macros for mlx5 IFC structures.
 *
 * Extracted from device.h so that code which cannot include device.h
 * (e.g. selftests) can still use the MLX5_SET/GET family directly.
 */

#ifndef MLX5_IFC_MACROS_H
#define MLX5_IFC_MACROS_H

/* Internal helpers -- 32-bit */
#define __mlx5_nullp(typ) ((struct mlx5_ifc_##typ##_bits *)0)
#define __mlx5_bit_sz(typ, fld) sizeof(__mlx5_nullp(typ)->fld)
#define __mlx5_bit_off(typ, fld) (offsetof(struct mlx5_ifc_##typ##_bits, fld))
#define __mlx5_16_off(typ, fld) (__mlx5_bit_off(typ, fld) / 16)
#define __mlx5_dw_off(typ, fld) (__mlx5_bit_off(typ, fld) / 32)
#define __mlx5_64_off(typ, fld) (__mlx5_bit_off(typ, fld) / 64)
#define __mlx5_16_bit_off(typ, fld) (16 - __mlx5_bit_sz(typ, fld) - (__mlx5_bit_off(typ, fld) & 0xf))
#define __mlx5_dw_bit_off(typ, fld) (32 - __mlx5_bit_sz(typ, fld) - (__mlx5_bit_off(typ, fld) & 0x1f))
#define __mlx5_mask(typ, fld) ((u32)((1ull << __mlx5_bit_sz(typ, fld)) - 1))
#define __mlx5_dw_mask(typ, fld) (__mlx5_mask(typ, fld) << __mlx5_dw_bit_off(typ, fld))
#define __mlx5_mask16(typ, fld) ((u16)((1ull << __mlx5_bit_sz(typ, fld)) - 1))
#define __mlx5_16_mask(typ, fld) (__mlx5_mask16(typ, fld) << __mlx5_16_bit_off(typ, fld))
#define __mlx5_st_sz_bits(typ) sizeof(struct mlx5_ifc_##typ##_bits)

/* Size and address macros */
#define MLX5_FLD_SZ_BYTES(typ, fld) (__mlx5_bit_sz(typ, fld) / 8)
#define MLX5_ST_SZ_BYTES(typ) (sizeof(struct mlx5_ifc_##typ##_bits) / 8)
#define MLX5_ST_SZ_DW(typ) (sizeof(struct mlx5_ifc_##typ##_bits) / 32)
#define MLX5_ST_SZ_QW(typ) (sizeof(struct mlx5_ifc_##typ##_bits) / 64)
#define MLX5_UN_SZ_BYTES(typ) (sizeof(union mlx5_ifc_##typ##_bits) / 8)
#define MLX5_UN_SZ_DW(typ) (sizeof(union mlx5_ifc_##typ##_bits) / 32)
#define MLX5_BYTE_OFF(typ, fld) (__mlx5_bit_off(typ, fld) / 8)
#define MLX5_ADDR_OF(typ, p, fld) ((void *)((u8 *)(p) + MLX5_BYTE_OFF(typ, fld)))

/* insert a value to a struct */
#define MLX5_SET(typ, p, fld, v) do { \
	u32 _v = v; \
	BUILD_BUG_ON(__mlx5_st_sz_bits(typ) % 32);             \
	*((__be32 *)(p) + __mlx5_dw_off(typ, fld)) = \
	cpu_to_be32((be32_to_cpu(*((__be32 *)(p) + __mlx5_dw_off(typ, fld))) & \
		     (~__mlx5_dw_mask(typ, fld))) | (((_v) & __mlx5_mask(typ, fld)) \
		     << __mlx5_dw_bit_off(typ, fld))); \
} while (0)

#define MLX5_ARRAY_SET(typ, p, fld, idx, v) do { \
	BUILD_BUG_ON(__mlx5_bit_off(typ, fld) % 32); \
	MLX5_SET(typ, p, fld[idx], v); \
} while (0)

#define MLX5_SET_TO_ONES(typ, p, fld) do { \
	BUILD_BUG_ON(__mlx5_st_sz_bits(typ) % 32);             \
	*((__be32 *)(p) + __mlx5_dw_off(typ, fld)) = \
	cpu_to_be32((be32_to_cpu(*((__be32 *)(p) + __mlx5_dw_off(typ, fld))) & \
		     (~__mlx5_dw_mask(typ, fld))) | ((__mlx5_mask(typ, fld)) \
		     << __mlx5_dw_bit_off(typ, fld))); \
} while (0)

#define MLX5_GET(typ, p, fld) ((be32_to_cpu(*((__be32 *)(p) +\
__mlx5_dw_off(typ, fld))) >> __mlx5_dw_bit_off(typ, fld)) & \
__mlx5_mask(typ, fld))

#define MLX5_GET_PR(typ, p, fld) ({ \
	u32 ___t = MLX5_GET(typ, p, fld); \
	pr_debug(#fld " = 0x%x\n", ___t); \
	___t; \
})

/* 64-bit field accessors */
#define __MLX5_SET64(typ, p, fld, v) do { \
	BUILD_BUG_ON(__mlx5_bit_sz(typ, fld) != 64); \
	*((__be64 *)(p) + __mlx5_64_off(typ, fld)) = cpu_to_be64(v); \
} while (0)

#define MLX5_SET64(typ, p, fld, v) do { \
	BUILD_BUG_ON(__mlx5_bit_off(typ, fld) % 64); \
	__MLX5_SET64(typ, p, fld, v); \
} while (0)

#define MLX5_ARRAY_SET64(typ, p, fld, idx, v) do { \
	BUILD_BUG_ON(__mlx5_bit_off(typ, fld) % 64); \
	__MLX5_SET64(typ, p, fld[idx], v); \
} while (0)

#define MLX5_ARRAY_GET64(typ, p, fld, idx)                                   \
	({                                                                   \
		BUILD_BUG_ON(__mlx5_bit_off(typ, fld) % 64);                 \
		be64_to_cpu(                                                 \
			*((__be64 *)(p) + __mlx5_64_off(typ, fld) + (idx))); \
	})

#define MLX5_GET64(typ, p, fld) be64_to_cpu(*((__be64 *)(p) + __mlx5_64_off(typ, fld)))

#define MLX5_GET64_PR(typ, p, fld) ({ \
	u64 ___t = MLX5_GET64(typ, p, fld); \
	pr_debug(#fld " = 0x%llx\n", ___t); \
	___t; \
})

/* 16-bit field accessors */
#define MLX5_GET16(typ, p, fld) ((be16_to_cpu(*((__be16 *)(p) +\
__mlx5_16_off(typ, fld))) >> __mlx5_16_bit_off(typ, fld)) & \
__mlx5_mask16(typ, fld))

#define MLX5_SET16(typ, p, fld, v) do { \
	u16 _v = v; \
	BUILD_BUG_ON(__mlx5_st_sz_bits(typ) % 16);             \
	*((__be16 *)(p) + __mlx5_16_off(typ, fld)) = \
	cpu_to_be16((be16_to_cpu(*((__be16 *)(p) + __mlx5_16_off(typ, fld))) & \
		     (~__mlx5_16_mask(typ, fld))) | (((_v) & __mlx5_mask16(typ, fld)) \
		     << __mlx5_16_bit_off(typ, fld))); \
} while (0)

/* Big endian getters */
#define MLX5_GET64_BE(typ, p, fld) (*((__be64 *)(p) +\
	__mlx5_64_off(typ, fld)))

#define MLX5_GET_BE(type_t, typ, p, fld) ({				  \
		type_t tmp;						  \
		switch (sizeof(tmp)) {					  \
		case sizeof(u8):					  \
			tmp = (__force type_t)MLX5_GET(typ, p, fld);	  \
			break;						  \
		case sizeof(u16):					  \
			tmp = (__force type_t)cpu_to_be16(MLX5_GET(typ, p, fld)); \
			break;						  \
		case sizeof(u32):					  \
			tmp = (__force type_t)cpu_to_be32(MLX5_GET(typ, p, fld)); \
			break;						  \
		case sizeof(u64):					  \
			tmp = (__force type_t)MLX5_GET64_BE(typ, p, fld); \
			break;						  \
			}						  \
		tmp;							  \
		})

/*
 * Use READ_ONCE/WRITE_ONCE for a single field that hardware may read/write
 * unpredictably, mostly owner bits. All other bits in the DW must be stable.
 * Usually a dma_wmb() will be required before a write and a dma_rmb() after a
 * read.
 */
#define MLX5_GET_ONCE(typ, p, fld)                                              \
	((be32_to_cpu(READ_ONCE(*((__be32 *)(p) + __mlx5_dw_off(typ, fld)))) >> \
	  __mlx5_dw_bit_off(typ, fld)) &                                        \
	 __mlx5_mask(typ, fld))

#define MLX5_SET_ONCE(typ, p, fld, v)                                      \
	do {                                                               \
		u32 _v = v;                                                \
		__be32 *_dw = (__be32 *)(p) + __mlx5_dw_off(typ, fld);     \
		BUILD_BUG_ON(__mlx5_st_sz_bits(typ) % 32);                 \
		WRITE_ONCE(*_dw,                                           \
			   cpu_to_be32((be32_to_cpu(READ_ONCE(*_dw)) &     \
					(~__mlx5_dw_mask(typ, fld))) |     \
				       (((_v) & __mlx5_mask(typ, fld))     \
					<< __mlx5_dw_bit_off(typ, fld)))); \
	} while (0)

/* Access MMIO registers, usually the init segment, using IFC structs. */
#define MLX5_GET_MMIO(typ, p, fld)                                         \
	((ioread32be(((__be32 __iomem *)(p) + __mlx5_dw_off(typ, fld))) >> \
	  __mlx5_dw_bit_off(typ, fld)) &                                   \
	 __mlx5_mask(typ, fld))

/* The set is not relaxed so there is an integrated dma_wmb(). */
#define MLX5_SET_MMIO(typ, p, fld, v)                                         \
	do {                                                                  \
		u32 _v = v;                                                   \
		void __iomem *_dw =                                           \
			((__be32 __iomem *)(p) + __mlx5_dw_off(typ, fld));    \
		if (__mlx5_bit_sz(typ, fld) == 32)                            \
			iowrite32be(_v, _dw);                                 \
		else                                                          \
			iowrite32be((ioread32be(_dw) &                        \
				     (~__mlx5_dw_mask(typ, fld))) |           \
					    ((_v & __mlx5_mask(typ, fld))     \
					     << __mlx5_dw_bit_off(typ, fld)), \
				    _dw);                                     \
	} while (0)

#endif /* MLX5_IFC_MACROS_H */
