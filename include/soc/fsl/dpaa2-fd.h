/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/*
 * Copyright 2014-2016 Freescale Semiconductor Inc.
 * Copyright 2016 NXP
 *
 */
#ifndef __FSL_DPAA2_FD_H
#define __FSL_DPAA2_FD_H

#include <linux/kernel.h>

/**
 * DOC: DPAA2 FD - Frame Descriptor APIs for DPAA2
 *
 * Frame Descriptors (FDs) are used to describe frame data in the DPAA2.
 * Frames can be enqueued and dequeued to Frame Queues (FQs) which are consumed
 * by the various DPAA accelerators (WRIOP, SEC, PME, DCE)
 *
 * There are three types of frames: single, scatter gather, and frame lists.
 *
 * The set of APIs in this file must be used to create, manipulate and
 * query Frame Descriptors.
 */

/**
 * struct dpaa2_fd - Struct describing FDs
 * @words:         for easier/faster copying the whole FD structure
 * @addr:          address in the FD
 * @len:           length in the FD
 * @bpid:          buffer pool ID
 * @format_offset: format, offset, and short-length fields
 * @frc:           frame context
 * @ctrl:          control bits...including dd, sc, va, err, etc
 * @flc:           flow context address
 *
 * This structure represents the basic Frame Descriptor used in the system.
 */
struct dpaa2_fd {
	union {
		u32 words[8];
		struct dpaa2_fd_simple {
			__le64 addr;
			__le32 len;
			__le16 bpid;
			__le16 format_offset;
			__le32 frc;
			__le32 ctrl;
			__le64 flc;
		} simple;
	};
};

#define FD_SHORT_LEN_FLAG_MASK	0x1
#define FD_SHORT_LEN_FLAG_SHIFT	14
#define FD_SHORT_LEN_MASK	0x3FFFF
#define FD_OFFSET_MASK		0x0FFF
#define FD_FORMAT_MASK		0x3
#define FD_FORMAT_SHIFT		12
#define FD_BPID_MASK		0x3FFF
#define SG_SHORT_LEN_FLAG_MASK	0x1
#define SG_SHORT_LEN_FLAG_SHIFT	14
#define SG_SHORT_LEN_MASK	0x1FFFF
#define SG_OFFSET_MASK		0x0FFF
#define SG_FORMAT_MASK		0x3
#define SG_FORMAT_SHIFT		12
#define SG_BPID_MASK		0x3FFF
#define SG_FINAL_FLAG_MASK	0x1
#define SG_FINAL_FLAG_SHIFT	15

/* Error bits in FD CTRL */
#define FD_CTRL_ERR_MASK	0x000000FF
#define FD_CTRL_UFD		0x00000004
#define FD_CTRL_SBE		0x00000008
#define FD_CTRL_FLC		0x00000010
#define FD_CTRL_FSE		0x00000020
#define FD_CTRL_FAERR		0x00000040

/* Annotation bits in FD CTRL */
#define FD_CTRL_PTA		0x00800000
#define FD_CTRL_PTV1		0x00400000

enum dpaa2_fd_format {
	dpaa2_fd_single = 0,
	dpaa2_fd_list,
	dpaa2_fd_sg
};

/**
 * dpaa2_fd_get_addr() - get the addr field of frame descriptor
 * @fd: the given frame descriptor
 *
 * Return the address in the frame descriptor.
 */
static inline dma_addr_t dpaa2_fd_get_addr(const struct dpaa2_fd *fd)
{
	return (dma_addr_t)le64_to_cpu(fd->simple.addr);
}

/**
 * dpaa2_fd_set_addr() - Set the addr field of frame descriptor
 * @fd: the given frame descriptor
 * @addr: the address needs to be set in frame descriptor
 */
static inline void dpaa2_fd_set_addr(struct dpaa2_fd *fd, dma_addr_t addr)
{
	fd->simple.addr = cpu_to_le64(addr);
}

/**
 * dpaa2_fd_get_frc() - Get the frame context in the frame descriptor
 * @fd: the given frame descriptor
 *
 * Return the frame context field in the frame descriptor.
 */
static inline u32 dpaa2_fd_get_frc(const struct dpaa2_fd *fd)
{
	return le32_to_cpu(fd->simple.frc);
}

/**
 * dpaa2_fd_set_frc() - Set the frame context in the frame descriptor
 * @fd: the given frame descriptor
 * @frc: the frame context needs to be set in frame descriptor
 */
static inline void dpaa2_fd_set_frc(struct dpaa2_fd *fd, u32 frc)
{
	fd->simple.frc = cpu_to_le32(frc);
}

/**
 * dpaa2_fd_get_ctrl() - Get the control bits in the frame descriptor
 * @fd: the given frame descriptor
 *
 * Return the control bits field in the frame descriptor.
 */
static inline u32 dpaa2_fd_get_ctrl(const struct dpaa2_fd *fd)
{
	return le32_to_cpu(fd->simple.ctrl);
}

/**
 * dpaa2_fd_set_ctrl() - Set the control bits in the frame descriptor
 * @fd: the given frame descriptor
 * @ctrl: the control bits to be set in the frame descriptor
 */
static inline void dpaa2_fd_set_ctrl(struct dpaa2_fd *fd, u32 ctrl)
{
	fd->simple.ctrl = cpu_to_le32(ctrl);
}

/**
 * dpaa2_fd_get_flc() - Get the flow context in the frame descriptor
 * @fd: the given frame descriptor
 *
 * Return the flow context in the frame descriptor.
 */
static inline dma_addr_t dpaa2_fd_get_flc(const struct dpaa2_fd *fd)
{
	return (dma_addr_t)le64_to_cpu(fd->simple.flc);
}

/**
 * dpaa2_fd_set_flc() - Set the flow context field of frame descriptor
 * @fd: the given frame descriptor
 * @flc_addr: the flow context needs to be set in frame descriptor
 */
static inline void dpaa2_fd_set_flc(struct dpaa2_fd *fd,  dma_addr_t flc_addr)
{
	fd->simple.flc = cpu_to_le64(flc_addr);
}

static inline bool dpaa2_fd_short_len(const struct dpaa2_fd *fd)
{
	return !!((le16_to_cpu(fd->simple.format_offset) >>
		  FD_SHORT_LEN_FLAG_SHIFT) & FD_SHORT_LEN_FLAG_MASK);
}

/**
 * dpaa2_fd_get_len() - Get the length in the frame descriptor
 * @fd: the given frame descriptor
 *
 * Return the length field in the frame descriptor.
 */
static inline u32 dpaa2_fd_get_len(const struct dpaa2_fd *fd)
{
	if (dpaa2_fd_short_len(fd))
		return le32_to_cpu(fd->simple.len) & FD_SHORT_LEN_MASK;

	return le32_to_cpu(fd->simple.len);
}

/**
 * dpaa2_fd_set_len() - Set the length field of frame descriptor
 * @fd: the given frame descriptor
 * @len: the length needs to be set in frame descriptor
 */
static inline void dpaa2_fd_set_len(struct dpaa2_fd *fd, u32 len)
{
	fd->simple.len = cpu_to_le32(len);
}

/**
 * dpaa2_fd_get_offset() - Get the offset field in the frame descriptor
 * @fd: the given frame descriptor
 *
 * Return the offset.
 */
static inline uint16_t dpaa2_fd_get_offset(const struct dpaa2_fd *fd)
{
	return le16_to_cpu(fd->simple.format_offset) & FD_OFFSET_MASK;
}

/**
 * dpaa2_fd_set_offset() - Set the offset field of frame descriptor
 * @fd: the given frame descriptor
 * @offset: the offset needs to be set in frame descriptor
 */
static inline void dpaa2_fd_set_offset(struct dpaa2_fd *fd, uint16_t offset)
{
	fd->simple.format_offset &= cpu_to_le16(~FD_OFFSET_MASK);
	fd->simple.format_offset |= cpu_to_le16(offset);
}

/**
 * dpaa2_fd_get_format() - Get the format field in the frame descriptor
 * @fd: the given frame descriptor
 *
 * Return the format.
 */
static inline enum dpaa2_fd_format dpaa2_fd_get_format(
						const struct dpaa2_fd *fd)
{
	return (enum dpaa2_fd_format)((le16_to_cpu(fd->simple.format_offset)
				      >> FD_FORMAT_SHIFT) & FD_FORMAT_MASK);
}

/**
 * dpaa2_fd_set_format() - Set the format field of frame descriptor
 * @fd: the given frame descriptor
 * @format: the format needs to be set in frame descriptor
 */
static inline void dpaa2_fd_set_format(struct dpaa2_fd *fd,
				       enum dpaa2_fd_format format)
{
	fd->simple.format_offset &=
		cpu_to_le16(~(FD_FORMAT_MASK << FD_FORMAT_SHIFT));
	fd->simple.format_offset |= cpu_to_le16(format << FD_FORMAT_SHIFT);
}

/**
 * dpaa2_fd_get_bpid() - Get the bpid field in the frame descriptor
 * @fd: the given frame descriptor
 *
 * Return the buffer pool id.
 */
static inline uint16_t dpaa2_fd_get_bpid(const struct dpaa2_fd *fd)
{
	return le16_to_cpu(fd->simple.bpid) & FD_BPID_MASK;
}

/**
 * dpaa2_fd_set_bpid() - Set the bpid field of frame descriptor
 * @fd: the given frame descriptor
 * @bpid: buffer pool id to be set
 */
static inline void dpaa2_fd_set_bpid(struct dpaa2_fd *fd, uint16_t bpid)
{
	fd->simple.bpid &= cpu_to_le16(~(FD_BPID_MASK));
	fd->simple.bpid |= cpu_to_le16(bpid);
}

/**
 * struct dpaa2_sg_entry - the scatter-gathering structure
 * @addr: address of the sg entry
 * @len: length in this sg entry
 * @bpid: buffer pool id
 * @format_offset: format and offset fields
 */
struct dpaa2_sg_entry {
	__le64 addr;
	__le32 len;
	__le16 bpid;
	__le16 format_offset;
};

enum dpaa2_sg_format {
	dpaa2_sg_single = 0,
	dpaa2_sg_frame_data,
	dpaa2_sg_sgt_ext
};

/* Accessors for SG entry fields */

/**
 * dpaa2_sg_get_addr() - Get the address from SG entry
 * @sg: the given scatter-gathering object
 *
 * Return the address.
 */
static inline dma_addr_t dpaa2_sg_get_addr(const struct dpaa2_sg_entry *sg)
{
	return (dma_addr_t)le64_to_cpu(sg->addr);
}

/**
 * dpaa2_sg_set_addr() - Set the address in SG entry
 * @sg: the given scatter-gathering object
 * @addr: the address to be set
 */
static inline void dpaa2_sg_set_addr(struct dpaa2_sg_entry *sg, dma_addr_t addr)
{
	sg->addr = cpu_to_le64(addr);
}

static inline bool dpaa2_sg_short_len(const struct dpaa2_sg_entry *sg)
{
	return !!((le16_to_cpu(sg->format_offset) >> SG_SHORT_LEN_FLAG_SHIFT)
		& SG_SHORT_LEN_FLAG_MASK);
}

/**
 * dpaa2_sg_get_len() - Get the length in SG entry
 * @sg: the given scatter-gathering object
 *
 * Return the length.
 */
static inline u32 dpaa2_sg_get_len(const struct dpaa2_sg_entry *sg)
{
	if (dpaa2_sg_short_len(sg))
		return le32_to_cpu(sg->len) & SG_SHORT_LEN_MASK;

	return le32_to_cpu(sg->len);
}

/**
 * dpaa2_sg_set_len() - Set the length in SG entry
 * @sg: the given scatter-gathering object
 * @len: the length to be set
 */
static inline void dpaa2_sg_set_len(struct dpaa2_sg_entry *sg, u32 len)
{
	sg->len = cpu_to_le32(len);
}

/**
 * dpaa2_sg_get_offset() - Get the offset in SG entry
 * @sg: the given scatter-gathering object
 *
 * Return the offset.
 */
static inline u16 dpaa2_sg_get_offset(const struct dpaa2_sg_entry *sg)
{
	return le16_to_cpu(sg->format_offset) & SG_OFFSET_MASK;
}

/**
 * dpaa2_sg_set_offset() - Set the offset in SG entry
 * @sg: the given scatter-gathering object
 * @offset: the offset to be set
 */
static inline void dpaa2_sg_set_offset(struct dpaa2_sg_entry *sg,
				       u16 offset)
{
	sg->format_offset &= cpu_to_le16(~SG_OFFSET_MASK);
	sg->format_offset |= cpu_to_le16(offset);
}

/**
 * dpaa2_sg_get_format() - Get the SG format in SG entry
 * @sg: the given scatter-gathering object
 *
 * Return the format.
 */
static inline enum dpaa2_sg_format
	dpaa2_sg_get_format(const struct dpaa2_sg_entry *sg)
{
	return (enum dpaa2_sg_format)((le16_to_cpu(sg->format_offset)
				       >> SG_FORMAT_SHIFT) & SG_FORMAT_MASK);
}

/**
 * dpaa2_sg_set_format() - Set the SG format in SG entry
 * @sg: the given scatter-gathering object
 * @format: the format to be set
 */
static inline void dpaa2_sg_set_format(struct dpaa2_sg_entry *sg,
				       enum dpaa2_sg_format format)
{
	sg->format_offset &= cpu_to_le16(~(SG_FORMAT_MASK << SG_FORMAT_SHIFT));
	sg->format_offset |= cpu_to_le16(format << SG_FORMAT_SHIFT);
}

/**
 * dpaa2_sg_get_bpid() - Get the buffer pool id in SG entry
 * @sg: the given scatter-gathering object
 *
 * Return the bpid.
 */
static inline u16 dpaa2_sg_get_bpid(const struct dpaa2_sg_entry *sg)
{
	return le16_to_cpu(sg->bpid) & SG_BPID_MASK;
}

/**
 * dpaa2_sg_set_bpid() - Set the buffer pool id in SG entry
 * @sg: the given scatter-gathering object
 * @bpid: the bpid to be set
 */
static inline void dpaa2_sg_set_bpid(struct dpaa2_sg_entry *sg, u16 bpid)
{
	sg->bpid &= cpu_to_le16(~(SG_BPID_MASK));
	sg->bpid |= cpu_to_le16(bpid);
}

/**
 * dpaa2_sg_is_final() - Check final bit in SG entry
 * @sg: the given scatter-gathering object
 *
 * Return bool.
 */
static inline bool dpaa2_sg_is_final(const struct dpaa2_sg_entry *sg)
{
	return !!(le16_to_cpu(sg->format_offset) >> SG_FINAL_FLAG_SHIFT);
}

/**
 * dpaa2_sg_set_final() - Set the final bit in SG entry
 * @sg: the given scatter-gathering object
 * @final: the final boolean to be set
 */
static inline void dpaa2_sg_set_final(struct dpaa2_sg_entry *sg, bool final)
{
	sg->format_offset &= cpu_to_le16((~(SG_FINAL_FLAG_MASK
					 << SG_FINAL_FLAG_SHIFT)) & 0xFFFF);
	sg->format_offset |= cpu_to_le16(final << SG_FINAL_FLAG_SHIFT);
}

#endif /* __FSL_DPAA2_FD_H */
