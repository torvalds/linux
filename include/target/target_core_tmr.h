#ifndef TARGET_CORE_TMR_H
#define TARGET_CORE_TMR_H

/* fabric independent task management function values */
enum tcm_tmreq_table {
	TMR_ABORT_TASK		= 1,
	TMR_ABORT_TASK_SET	= 2,
	TMR_CLEAR_ACA		= 3,
	TMR_CLEAR_TASK_SET	= 4,
	TMR_LUN_RESET		= 5,
	TMR_TARGET_WARM_RESET	= 6,
	TMR_TARGET_COLD_RESET	= 7,
	TMR_FABRIC_TMR		= 255,
};

/* fabric independent task management response values */
enum tcm_tmrsp_table {
	TMR_FUNCTION_COMPLETE		= 0,
	TMR_TASK_DOES_NOT_EXIST		= 1,
	TMR_LUN_DOES_NOT_EXIST		= 2,
	TMR_TASK_STILL_ALLEGIANT	= 3,
	TMR_TASK_FAILOVER_NOT_SUPPORTED	= 4,
	TMR_TASK_MGMT_FUNCTION_NOT_SUPPORTED	= 5,
	TMR_FUNCTION_AUTHORIZATION_FAILED = 6,
	TMR_FUNCTION_REJECTED		= 255,
};

extern struct kmem_cache *se_tmr_req_cache;

extern struct se_tmr_req *core_tmr_alloc_req(struct se_cmd *, void *, u8);
extern void core_tmr_release_req(struct se_tmr_req *);
extern int core_tmr_lun_reset(struct se_device *, struct se_tmr_req *,
				struct list_head *, struct se_cmd *);

#endif /* TARGET_CORE_TMR_H */
