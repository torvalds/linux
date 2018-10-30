/* SPDX-License-Identifier: GPL-2.0 */
#ifndef TARGET_CORE_BACKEND_H
#define TARGET_CORE_BACKEND_H

#include <linux/types.h>
#include <asm/unaligned.h>
#include <target/target_core_base.h>

#define TRANSPORT_FLAG_PASSTHROUGH		0x1
/*
 * ALUA commands, state checks and setup operations are handled by the
 * backend module.
 */
#define TRANSPORT_FLAG_PASSTHROUGH_ALUA		0x2
#define TRANSPORT_FLAG_PASSTHROUGH_PGR          0x4

struct request_queue;
struct scatterlist;

struct target_backend_ops {
	char name[16];
	char inquiry_prod[16];
	char inquiry_rev[4];
	struct module *owner;

	u8 transport_flags;

	int (*attach_hba)(struct se_hba *, u32);
	void (*detach_hba)(struct se_hba *);
	int (*pmode_enable_hba)(struct se_hba *, unsigned long);

	struct se_device *(*alloc_device)(struct se_hba *, const char *);
	int (*configure_device)(struct se_device *);
	void (*destroy_device)(struct se_device *);
	void (*free_device)(struct se_device *device);

	ssize_t (*set_configfs_dev_params)(struct se_device *,
					   const char *, ssize_t);
	ssize_t (*show_configfs_dev_params)(struct se_device *, char *);

	sense_reason_t (*parse_cdb)(struct se_cmd *cmd);
	u32 (*get_device_type)(struct se_device *);
	sector_t (*get_blocks)(struct se_device *);
	sector_t (*get_alignment_offset_lbas)(struct se_device *);
	/* lbppbe = logical blocks per physical block exponent. see SBC-3 */
	unsigned int (*get_lbppbe)(struct se_device *);
	unsigned int (*get_io_min)(struct se_device *);
	unsigned int (*get_io_opt)(struct se_device *);
	unsigned char *(*get_sense_buffer)(struct se_cmd *);
	bool (*get_write_cache)(struct se_device *);
	int (*init_prot)(struct se_device *);
	int (*format_prot)(struct se_device *);
	void (*free_prot)(struct se_device *);

	struct configfs_attribute **tb_dev_attrib_attrs;
	struct configfs_attribute **tb_dev_action_attrs;
};

struct sbc_ops {
	sense_reason_t (*execute_rw)(struct se_cmd *cmd, struct scatterlist *,
				     u32, enum dma_data_direction);
	sense_reason_t (*execute_sync_cache)(struct se_cmd *cmd);
	sense_reason_t (*execute_write_same)(struct se_cmd *cmd);
	sense_reason_t (*execute_unmap)(struct se_cmd *cmd,
				sector_t lba, sector_t nolb);
};

int	transport_backend_register(const struct target_backend_ops *);
void	target_backend_unregister(const struct target_backend_ops *);

void	target_complete_cmd(struct se_cmd *, u8);
void	target_complete_cmd_with_length(struct se_cmd *, u8, int);

void	transport_copy_sense_to_cmd(struct se_cmd *, unsigned char *);

sense_reason_t	spc_parse_cdb(struct se_cmd *cmd, unsigned int *size);
sense_reason_t	spc_emulate_report_luns(struct se_cmd *cmd);
sense_reason_t	spc_emulate_inquiry_std(struct se_cmd *, unsigned char *);
sense_reason_t	spc_emulate_evpd_83(struct se_cmd *, unsigned char *);

sense_reason_t	sbc_parse_cdb(struct se_cmd *cmd, struct sbc_ops *ops);
u32	sbc_get_device_rev(struct se_device *dev);
u32	sbc_get_device_type(struct se_device *dev);
sector_t	sbc_get_write_same_sectors(struct se_cmd *cmd);
void	sbc_dif_generate(struct se_cmd *);
sense_reason_t	sbc_dif_verify(struct se_cmd *, sector_t, unsigned int,
				     unsigned int, struct scatterlist *, int);
void sbc_dif_copy_prot(struct se_cmd *, unsigned int, bool,
		       struct scatterlist *, int);
void	transport_set_vpd_proto_id(struct t10_vpd *, unsigned char *);
int	transport_set_vpd_assoc(struct t10_vpd *, unsigned char *);
int	transport_set_vpd_ident_type(struct t10_vpd *, unsigned char *);
int	transport_set_vpd_ident(struct t10_vpd *, unsigned char *);

extern struct configfs_attribute *sbc_attrib_attrs[];
extern struct configfs_attribute *passthrough_attrib_attrs[];

/* core helpers also used by command snooping in pscsi */
void	*transport_kmap_data_sg(struct se_cmd *);
void	transport_kunmap_data_sg(struct se_cmd *);
/* core helpers also used by xcopy during internal command setup */
sense_reason_t	transport_generic_map_mem_to_cmd(struct se_cmd *,
		struct scatterlist *, u32, struct scatterlist *, u32);

bool	target_lun_is_rdonly(struct se_cmd *);
sense_reason_t passthrough_parse_cdb(struct se_cmd *cmd,
	sense_reason_t (*exec_cmd)(struct se_cmd *cmd));

bool target_sense_desc_format(struct se_device *dev);
sector_t target_to_linux_sector(struct se_device *dev, sector_t lb);
bool target_configure_unmap_from_queue(struct se_dev_attrib *attrib,
				       struct request_queue *q);

static inline bool target_dev_configured(struct se_device *se_dev)
{
	return !!(se_dev->dev_flags & DF_CONFIGURED);
}

/* Only use get_unaligned_be24() if reading p - 1 is allowed. */
static inline uint32_t get_unaligned_be24(const uint8_t *const p)
{
	return get_unaligned_be32(p - 1) & 0xffffffU;
}

#endif /* TARGET_CORE_BACKEND_H */
