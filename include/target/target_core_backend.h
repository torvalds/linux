#ifndef TARGET_CORE_BACKEND_H
#define TARGET_CORE_BACKEND_H

#define TRANSPORT_FLAG_PASSTHROUGH		1

struct target_backend_cits {
	struct config_item_type tb_dev_cit;
	struct config_item_type tb_dev_attrib_cit;
	struct config_item_type tb_dev_pr_cit;
	struct config_item_type tb_dev_wwn_cit;
	struct config_item_type tb_dev_alua_tg_pt_gps_cit;
	struct config_item_type tb_dev_stat_cit;
};

struct se_subsystem_api {
	struct list_head sub_api_list;

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
	void (*free_device)(struct se_device *device);

	ssize_t (*set_configfs_dev_params)(struct se_device *,
					   const char *, ssize_t);
	ssize_t (*show_configfs_dev_params)(struct se_device *, char *);

	void (*transport_complete)(struct se_cmd *cmd,
				   struct scatterlist *,
				   unsigned char *);

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

	struct target_backend_cits tb_cits;
};

struct sbc_ops {
	sense_reason_t (*execute_rw)(struct se_cmd *cmd, struct scatterlist *,
				     u32, enum dma_data_direction);
	sense_reason_t (*execute_sync_cache)(struct se_cmd *cmd);
	sense_reason_t (*execute_write_same)(struct se_cmd *cmd);
	sense_reason_t (*execute_write_same_unmap)(struct se_cmd *cmd);
	sense_reason_t (*execute_unmap)(struct se_cmd *cmd);
};

int	transport_subsystem_register(struct se_subsystem_api *);
void	transport_subsystem_release(struct se_subsystem_api *);

void	target_complete_cmd(struct se_cmd *, u8);
void	target_complete_cmd_with_length(struct se_cmd *, u8, int);

sense_reason_t	spc_parse_cdb(struct se_cmd *cmd, unsigned int *size);
sense_reason_t	spc_emulate_report_luns(struct se_cmd *cmd);
sense_reason_t	spc_emulate_inquiry_std(struct se_cmd *, unsigned char *);
sense_reason_t	spc_emulate_evpd_83(struct se_cmd *, unsigned char *);

sense_reason_t	sbc_parse_cdb(struct se_cmd *cmd, struct sbc_ops *ops);
u32	sbc_get_device_rev(struct se_device *dev);
u32	sbc_get_device_type(struct se_device *dev);
sector_t	sbc_get_write_same_sectors(struct se_cmd *cmd);
sense_reason_t sbc_execute_unmap(struct se_cmd *cmd,
	sense_reason_t (*do_unmap_fn)(struct se_cmd *cmd, void *priv,
				      sector_t lba, sector_t nolb),
	void *priv);
void	sbc_dif_generate(struct se_cmd *);
sense_reason_t	sbc_dif_verify_write(struct se_cmd *, sector_t, unsigned int,
				     unsigned int, struct scatterlist *, int);
sense_reason_t	sbc_dif_verify_read(struct se_cmd *, sector_t, unsigned int,
				    unsigned int, struct scatterlist *, int);
sense_reason_t	sbc_dif_read_strip(struct se_cmd *);

void	transport_set_vpd_proto_id(struct t10_vpd *, unsigned char *);
int	transport_set_vpd_assoc(struct t10_vpd *, unsigned char *);
int	transport_set_vpd_ident_type(struct t10_vpd *, unsigned char *);
int	transport_set_vpd_ident(struct t10_vpd *, unsigned char *);

/* core helpers also used by command snooping in pscsi */
void	*transport_kmap_data_sg(struct se_cmd *);
void	transport_kunmap_data_sg(struct se_cmd *);
/* core helpers also used by xcopy during internal command setup */
int	target_alloc_sgl(struct scatterlist **, unsigned int *, u32, bool);
sense_reason_t	transport_generic_map_mem_to_cmd(struct se_cmd *,
		struct scatterlist *, u32, struct scatterlist *, u32);

void	array_free(void *array, int n);

/* From target_core_configfs.c to setup default backend config_item_types */
void	target_core_setup_sub_cits(struct se_subsystem_api *);

/* attribute helpers from target_core_device.c for backend drivers */
bool	se_dev_check_wce(struct se_device *);
int	se_dev_set_max_unmap_lba_count(struct se_device *, u32);
int	se_dev_set_max_unmap_block_desc_count(struct se_device *, u32);
int	se_dev_set_unmap_granularity(struct se_device *, u32);
int	se_dev_set_unmap_granularity_alignment(struct se_device *, u32);
int	se_dev_set_max_write_same_len(struct se_device *, u32);
int	se_dev_set_emulate_model_alias(struct se_device *, int);
int	se_dev_set_emulate_dpo(struct se_device *, int);
int	se_dev_set_emulate_fua_write(struct se_device *, int);
int	se_dev_set_emulate_fua_read(struct se_device *, int);
int	se_dev_set_emulate_write_cache(struct se_device *, int);
int	se_dev_set_emulate_ua_intlck_ctrl(struct se_device *, int);
int	se_dev_set_emulate_tas(struct se_device *, int);
int	se_dev_set_emulate_tpu(struct se_device *, int);
int	se_dev_set_emulate_tpws(struct se_device *, int);
int	se_dev_set_emulate_caw(struct se_device *, int);
int	se_dev_set_emulate_3pc(struct se_device *, int);
int	se_dev_set_pi_prot_type(struct se_device *, int);
int	se_dev_set_pi_prot_format(struct se_device *, int);
int	se_dev_set_enforce_pr_isids(struct se_device *, int);
int	se_dev_set_force_pr_aptpl(struct se_device *, int);
int	se_dev_set_is_nonrot(struct se_device *, int);
int	se_dev_set_emulate_rest_reord(struct se_device *dev, int);
int	se_dev_set_queue_depth(struct se_device *, u32);
int	se_dev_set_max_sectors(struct se_device *, u32);
int	se_dev_set_optimal_sectors(struct se_device *, u32);
int	se_dev_set_block_size(struct se_device *, u32);
sense_reason_t passthrough_parse_cdb(struct se_cmd *cmd,
	sense_reason_t (*exec_cmd)(struct se_cmd *cmd));

#endif /* TARGET_CORE_BACKEND_H */
