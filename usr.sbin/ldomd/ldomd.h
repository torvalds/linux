extern struct ds_service var_config_service;

struct guest {
	const char *name;
	uint64_t gid;
	uint64_t mdpa;

	struct md_node *node;
	struct md *md;

	TAILQ_ENTRY(guest) link;
};

void hv_update_md(struct guest *);
