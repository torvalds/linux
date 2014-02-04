#ifndef TARGET_CORE_BACKEND_H
#define TARGET_CORE_BACKEND_H

#define TRANSPORT_PLUGIN_PHBA_PDEV		1
#define TRANSPORT_PLUGIN_VHBA_PDEV		2
#define TRANSPORT_PLUGIN_VHBA_VDEV		3

struct se_subsystem_api {
	struct list_head sub_api_list;

	char name[16];
	char inquiry_prod[16];
	char inquiry_rev[4];
	struct module *owner;

	u8 transport_type;

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
sense_reason_t	sbc_dif_verify_write(struct se_cmd *, sector_t, unsigned int,
				     unsigned int, struct scatterlist *, int);
sense_reason_t	sbc_dif_verify_read(struct se_cmd *, sector_t, unsigned int,
				    unsigned int, struct scatterlist *, int);

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

#endif /* TARGET_CORE_BACKEND_H */
