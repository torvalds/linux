#ifndef TARGET_CORE_BACKEND_H
#define TARGET_CORE_BACKEND_H

#define TRANSPORT_PLUGIN_PHBA_PDEV		1
#define TRANSPORT_PLUGIN_VHBA_PDEV		2
#define TRANSPORT_PLUGIN_VHBA_VDEV		3

struct se_subsystem_api {
	struct list_head sub_api_list;

	char name[16];
	struct module *owner;

	u8 transport_type;

	int (*attach_hba)(struct se_hba *, u32);
	void (*detach_hba)(struct se_hba *);
	int (*pmode_enable_hba)(struct se_hba *, unsigned long);
	void *(*allocate_virtdevice)(struct se_hba *, const char *);
	struct se_device *(*create_virtdevice)(struct se_hba *,
				struct se_subsystem_dev *, void *);
	void (*free_device)(void *);
	void (*transport_complete)(struct se_cmd *cmd,
				   struct scatterlist *,
				   unsigned char *);

	int (*parse_cdb)(struct se_cmd *cmd);
	ssize_t (*check_configfs_dev_params)(struct se_hba *,
			struct se_subsystem_dev *);
	ssize_t (*set_configfs_dev_params)(struct se_hba *,
			struct se_subsystem_dev *, const char *, ssize_t);
	ssize_t (*show_configfs_dev_params)(struct se_hba *,
			struct se_subsystem_dev *, char *);
	u32 (*get_device_rev)(struct se_device *);
	u32 (*get_device_type)(struct se_device *);
	sector_t (*get_blocks)(struct se_device *);
	unsigned char *(*get_sense_buffer)(struct se_cmd *);
};

struct spc_ops {
	int (*execute_rw)(struct se_cmd *cmd);
	int (*execute_sync_cache)(struct se_cmd *cmd);
	int (*execute_write_same)(struct se_cmd *cmd);
	int (*execute_unmap)(struct se_cmd *cmd);
};

int	transport_subsystem_register(struct se_subsystem_api *);
void	transport_subsystem_release(struct se_subsystem_api *);

struct se_device *transport_add_device_to_core_hba(struct se_hba *,
		struct se_subsystem_api *, struct se_subsystem_dev *, u32,
		void *, struct se_dev_limits *, const char *, const char *);

void	target_complete_cmd(struct se_cmd *, u8);

int	sbc_parse_cdb(struct se_cmd *cmd, struct spc_ops *ops);
int	spc_parse_cdb(struct se_cmd *cmd, unsigned int *size);
int	spc_get_write_same_sectors(struct se_cmd *cmd);

void	transport_set_vpd_proto_id(struct t10_vpd *, unsigned char *);
int	transport_set_vpd_assoc(struct t10_vpd *, unsigned char *);
int	transport_set_vpd_ident_type(struct t10_vpd *, unsigned char *);
int	transport_set_vpd_ident(struct t10_vpd *, unsigned char *);

/* core helpers also used by command snooping in pscsi */
void	*transport_kmap_data_sg(struct se_cmd *);
void	transport_kunmap_data_sg(struct se_cmd *);

void	array_free(void *array, int n);

#endif /* TARGET_CORE_BACKEND_H */
