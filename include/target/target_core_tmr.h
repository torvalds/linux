#ifndef TARGET_CORE_TMR_H
#define TARGET_CORE_TMR_H

/* task management function values */
#ifdef ABORT_TASK
#undef ABORT_TASK
#endif /* ABORT_TASK */
#define ABORT_TASK				1
#ifdef ABORT_TASK_SET
#undef ABORT_TASK_SET
#endif /* ABORT_TASK_SET */
#define ABORT_TASK_SET				2
#ifdef CLEAR_ACA
#undef CLEAR_ACA
#endif /* CLEAR_ACA */
#define CLEAR_ACA				3
#ifdef CLEAR_TASK_SET
#undef CLEAR_TASK_SET
#endif /* CLEAR_TASK_SET */
#define CLEAR_TASK_SET				4
#define LUN_RESET				5
#define TARGET_WARM_RESET			6
#define TARGET_COLD_RESET			7
#define TASK_REASSIGN				8

/* task management response values */
#define TMR_FUNCTION_COMPLETE			0
#define TMR_TASK_DOES_NOT_EXIST			1
#define TMR_LUN_DOES_NOT_EXIST			2
#define TMR_TASK_STILL_ALLEGIANT		3
#define TMR_TASK_FAILOVER_NOT_SUPPORTED		4
#define TMR_TASK_MGMT_FUNCTION_NOT_SUPPORTED	5
#define TMR_FUNCTION_AUTHORIZATION_FAILED	6
#define TMR_FUNCTION_REJECTED			255

extern struct kmem_cache *se_tmr_req_cache;

extern struct se_tmr_req *core_tmr_alloc_req(struct se_cmd *, void *, u8);
extern void core_tmr_release_req(struct se_tmr_req *);
extern int core_tmr_lun_reset(struct se_device *, struct se_tmr_req *,
				struct list_head *, struct se_cmd *);

#endif /* TARGET_CORE_TMR_H */
