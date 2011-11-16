/*
 * Copyright (C) 2010-2011 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_RENDERCORE_H__
#define __MALI_RENDERCORE_H__

#include "mali_osk.h"
#include "mali_kernel_common.h"
#include "mali_kernel_subsystem.h"

#define PRIORITY_LEVELS 3
#define PRIORITY_MAX 0
#define PRIORITY_MIN (PRIORITY_MAX+PRIORITY_LEVELS-1)

/* This file contains what we need in kernel for all core types. */

typedef enum
{
	CORE_IDLE,              /**< Core is ready for a new job */
	CORE_WORKING,           /**< Core is working on a job */
	CORE_WATCHDOG_TIMEOUT,  /**< Core is working but it has timed out */
	CORE_POLL,              /**< Poll timer triggered, pending handling */
	CORE_HANG_CHECK_TIMEOUT,/**< Timeout for hang detection */
	CORE_OFF                /**< Core is powered off */
} mali_core_status;

typedef enum
{
	SUBSYSTEM_RESCHEDULE,
	SUBSYSTEM_WAIT
} mali_subsystem_reschedule_option;

typedef enum
{
	MALI_CORE_RESET_STYLE_RUNABLE,
	MALI_CORE_RESET_STYLE_DISABLE,
	MALI_CORE_RESET_STYLE_HARD
} mali_core_reset_style;

typedef enum
{
	JOB_STATUS_CONTINUE_RUN			= 0x01,
	JOB_STATUS_END_SUCCESS			= 1<<(16+0),
	JOB_STATUS_END_OOM				= 1<<(16+1),
	JOB_STATUS_END_ABORT			= 1<<(16+2),
	JOB_STATUS_END_TIMEOUT_SW		= 1<<(16+3),
	JOB_STATUS_END_HANG				= 1<<(16+4),
	JOB_STATUS_END_SEG_FAULT		= 1<<(16+5),
	JOB_STATUS_END_ILLEGAL_JOB		= 1<<(16+6),
	JOB_STATUS_END_UNKNOWN_ERR		= 1<<(16+7),
	JOB_STATUS_END_SHUTDOWN			= 1<<(16+8),
	JOB_STATUS_END_SYSTEM_UNUSABLE	= 1<<(16+9)
} mali_subsystem_job_end_code;


struct mali_core_job;
struct mali_core_subsystem;
struct mali_core_renderunit;
struct mali_core_session;

/* We have one of these subsystems for each core type */
typedef struct mali_core_subsystem
{
	struct mali_core_renderunit ** mali_core_array; /* An array of all cores of this type */
	u32 number_of_cores; /* Number of cores in this list */

	_mali_core_type core_type;

	u32 magic_nr;

	_mali_osk_list_t renderunit_idle_head; /* Idle cores of this type */
	_mali_osk_list_t renderunit_off_head;  /* Powered off cores of this type */

	/* Linked list for each priority of sessions with a job ready for scheduelling */
	_mali_osk_list_t awaiting_sessions_head[PRIORITY_LEVELS];
	u32 awaiting_sessions_sum_all_priorities;

	/* Linked list of all sessions connected to this coretype */
	_mali_osk_list_t all_sessions_head;

	/* Linked list of all sessions connected to this coretype */
    struct _mali_osk_notification_queue_t * notification_queue;

	const char * name;
	mali_kernel_subsystem_identifier id;

	/**** Functions registered for this core type. Set during mali_core_init ******/
	/* Start this job on this core. Return MALI_TRUE if the job was started. */
	_mali_osk_errcode_t (*start_job)(struct mali_core_job * job, struct mali_core_renderunit * core);

	/* Check if given core has an interrupt pending. Return MALI_TRUE and set mask to 0 if pending */
	u32 (*irq_handler_upper_half)(struct mali_core_renderunit * core);

	/* This function should check if the interrupt indicates that job was finished.
	If so it should update the job-struct, reset the core registers, and return MALI_TRUE, .
	If the job is still working after this function it should return MALI_FALSE.
	The function must also enable the bits in the interrupt mask for the core.
	Called by the bottom half interrupt function.	*/
	int (*irq_handler_bottom_half)(struct mali_core_renderunit* core);

	/* This function is called from the ioctl function and should return a mali_core_job pointer
	to a created mali_core_job object with the data given from userspace */
	_mali_osk_errcode_t (*get_new_job_from_user)(struct mali_core_session * session, void * argument);

	_mali_osk_errcode_t (*suspend_response)(struct mali_core_session * session, void * argument);

	/* This function is called from the ioctl function and should write the necessary data
	 to userspace telling which job was finished and the status and debuginfo for this job.
	 The function must also free and cleanup the input job object. */
	void (*return_job_to_user)(struct mali_core_job * job, mali_subsystem_job_end_code end_status);

	/* Is called when a subsystem shuts down. This function needs to
	   release internal pointers in the core struct, and free the
	   core struct before returning.
	   It is not allowed to write to any registers, since this
	   unmapping is already done. */
	void (*renderunit_delete)(struct mali_core_renderunit * core);

	/* Is called when we want to abort a job that is running on the core.
	   This is done if program exits while core is running */
	void (*reset_core)(struct mali_core_renderunit * core, mali_core_reset_style style);

	/* Is called when the rendercore wants the core to give an interrupt */
	void (*probe_core_irq_trigger)(struct mali_core_renderunit* core);

	/* Is called when the irq probe wants the core to acknowledge an interrupt from the hw */
	_mali_osk_errcode_t (*probe_core_irq_acknowledge)(struct mali_core_renderunit* core);

	/* Called when the rendercore want to issue a bus stop request to a core */
	void (*stop_bus)(struct mali_core_renderunit* core);
} mali_core_subsystem;


/* Per core data. This must be embedded into each core type internal core info. */
typedef struct mali_core_renderunit
{
	struct mali_core_subsystem * subsystem; /* The core belongs to this subsystem */
	_mali_osk_list_t list;                  /* Is always in subsystem->idle_list OR session->renderunits_working */
	mali_core_status  state;
	mali_bool error_recovery;               /* Indicates if the core is waiting for external help to recover (typically the MMU) */
	mali_bool in_detach_function;
	struct mali_core_job * current_job;     /* Current job being processed on this core ||NULL */
	u32 magic_nr;
 	_mali_osk_timer_t * timer;
	_mali_osk_timer_t * timer_hang_detection;

	mali_io_address registers_mapped;       /* IO-mapped pointer to registers */
	u32   registers_base_addr;              /* Base addres of the registers */
	u32 size;                               /* The size of registers_mapped */
	const char * description;               /* Description of this core. */
	u32 irq_nr;                             /* The IRQ nr for this core */
	u32 core_version;
#if USING_MMU
	u32 mmu_id;
	void * mmu;                             /* The MMU this rendercore is behind.*/
#endif
#if USING_MALI_PMM
	mali_pmm_core_id pmm_id;                /* The PMM core id */
	mali_bool pend_power_down;              /* Power down is requested */
#endif

	u32 core_number;                        /* 0 for first detected core of this type, 1 for second and so on */

    _mali_osk_irq_t *irq;
} mali_core_renderunit;


/* Per open FILE data. */
/* You must held subsystem->mutex before any transactions to this datatype. */
typedef struct mali_core_session
{
	struct mali_core_subsystem * subsystem;	   /* The session belongs to this subsystem */
	_mali_osk_list_t renderunits_working_head; /* List of renderunits working for this session */
	struct mali_core_job *job_waiting_to_run;  /* The next job from this session to run */

	_mali_osk_list_t awaiting_sessions_list; /* Linked list of sessions with jobs, for each priority */
	_mali_osk_list_t all_sessions_list;      /* Linked list of all sessions on the system. */

    _mali_osk_notification_queue_t * notification_queue; /* Messages back to Base in userspace*/
#if USING_MMU
	struct mali_session_data * mmu_session; /* The session associated with the MMU page tables for this core */
#endif
	u32 magic_nr;
#if MALI_STATE_TRACKING
	_mali_osk_atomic_t jobs_received;
	_mali_osk_atomic_t jobs_started;
	_mali_osk_atomic_t jobs_ended;
	_mali_osk_atomic_t jobs_returned;
	u32 pid;
#endif
} mali_core_session;


/* This must be embedded into a specific mali_core_job struct */
/* use this macro to get spesific mali_core_job:  container_of(ptr, type, member)*/
typedef struct mali_core_job
{
	_mali_osk_list_t list; /* Linked list of jobs. Used by struct mali_core_session */
	struct mali_core_session *session;
	u32 magic_nr;
	u32 priority;
	u32 watchdog_msecs;
	u32 render_time_msecs ;
	u32 start_time_jiffies;
	unsigned long watchdog_jiffies;
	u32 abort_id;
	u32 job_nr;
} mali_core_job;

/*
 * The rendercode subsystem is included in the subsystems[] array.
 */
extern struct mali_kernel_subsystem mali_subsystem_rendercore;

void subsystem_flush_mapped_mem_cache(void);


#define SUBSYSTEM_MAGIC_NR 	0xdeadbeef
#define CORE_MAGIC_NR 		0xcafebabe
#define SESSION_MAGIC_NR 	0xbabe1234
#define JOB_MAGIC_NR 		0x0123abcd


#define MALI_CHECK_SUBSYSTEM(subsystem)\
	do { \
		if ( SUBSYSTEM_MAGIC_NR != subsystem->magic_nr) MALI_PRINT_ERROR(("Wrong magic number"));\
	} while (0)

#define MALI_CHECK_CORE(CORE)\
	do { \
		if ( CORE_MAGIC_NR != CORE->magic_nr) MALI_PRINT_ERROR(("Wrong magic number"));\
} while (0)

#define MALI_CHECK_SESSION(SESSION)\
	do { \
		if ( SESSION_MAGIC_NR != SESSION->magic_nr) MALI_PRINT_ERROR(("Wrong magic number"));\
} while (0)

#define MALI_CHECK_JOB(JOB)\
	do { \
		if ( JOB_MAGIC_NR != JOB->magic_nr) MALI_PRINT_ERROR(("Wrong magic number"));\
} while (0)


/* Check if job_a has higher priority than job_b */
MALI_STATIC_INLINE int job_has_higher_priority(mali_core_job * job_a, mali_core_job * job_b)
{
	/* The lowest number has the highest priority */
	return (int) (job_a->priority < job_b->priority);
}

MALI_STATIC_INLINE void job_priority_set(mali_core_job * job, u32 priority)
{
	if (priority > PRIORITY_MIN) job->priority = PRIORITY_MIN;
	else job->priority = priority;
}

void job_watchdog_set(mali_core_job * job, u32 watchdog_msecs);

/* For use by const default register settings (e.g. set these after reset) */
typedef struct register_address_and_value
{
	u32 address;
	u32 value;
} register_address_and_value ;


/* For use by dynamic default register settings (e.g. set these after reset) */
typedef struct register_address_and_value_list
{
	_mali_osk_list_t list;
	register_address_and_value item;
} register_address_and_value_list ;

/* Used if the user wants to set a continious block of registers */
typedef struct register_array_user
{
	u32 entries_in_array;
	u32 start_address;
	void __user * reg_array;
}register_array_user;


#define MALI_CORE_SUBSYSTEM_MUTEX_GRAB(subsys) \
	do { \
		MALI_DEBUG_PRINT(5, ("MUTEX: GRAB %s() %d on %s\n",__FUNCTION__, __LINE__, subsys->name)); \
		_mali_osk_lock_wait( rendercores_global_mutex, _MALI_OSK_LOCKMODE_RW); \
		MALI_DEBUG_PRINT(5, ("MUTEX: GRABBED %s() %d on %s\n",__FUNCTION__, __LINE__, subsys->name)); \
		if ( SUBSYSTEM_MAGIC_NR != subsys->magic_nr ) MALI_PRINT_ERROR(("Wrong magic number"));\
		rendercores_global_mutex_is_held = 1; \
		rendercores_global_mutex_owner = _mali_osk_get_tid();  \
	} while (0) ;

#define MALI_CORE_SUBSYSTEM_MUTEX_RELEASE(subsys) \
	do { \
		MALI_DEBUG_PRINT(5, ("MUTEX: RELEASE %s() %d on %s\n",__FUNCTION__, __LINE__, subsys->name)); \
		rendercores_global_mutex_is_held = 0; \
		 rendercores_global_mutex_owner = 0; \
		if ( SUBSYSTEM_MAGIC_NR != subsys->magic_nr ) MALI_PRINT_ERROR(("Wrong magic number"));\
		_mali_osk_lock_signal( rendercores_global_mutex, _MALI_OSK_LOCKMODE_RW); \
		MALI_DEBUG_PRINT(5, ("MUTEX: RELEASED %s() %d on %s\n",__FUNCTION__, __LINE__, subsys->name)); \
		if ( SUBSYSTEM_MAGIC_NR != subsys->magic_nr ) MALI_PRINT_ERROR(("Wrong magic number"));\
	} while (0) ;


#define MALI_ASSERT_MUTEX_IS_GRABBED(input_pointer)\
	do { \
		if ( 0 == rendercores_global_mutex_is_held ) MALI_PRINT_ERROR(("ASSERT MUTEX SHOULD BE GRABBED"));\
		if ( SUBSYSTEM_MAGIC_NR != input_pointer->magic_nr ) MALI_PRINT_ERROR(("Wrong magic number"));\
		if ( rendercores_global_mutex_owner != _mali_osk_get_tid() ) MALI_PRINT_ERROR(("Owner mismatch"));\
	} while (0)


u32   mali_core_renderunit_register_read(struct mali_core_renderunit *core, u32 relative_address);
void  mali_core_renderunit_register_read_array(struct mali_core_renderunit *core, u32 relative_address,  u32 * result_array, u32 nr_of_regs);
void  mali_core_renderunit_register_write(struct mali_core_renderunit *core, u32 relative_address, u32 new_val);
void  mali_core_renderunit_register_write_array(struct mali_core_renderunit *core, u32 relative_address,  u32 * write_array, u32 nr_of_regs);

_mali_osk_errcode_t  mali_core_renderunit_init(struct mali_core_renderunit * core);
void  mali_core_renderunit_term(struct mali_core_renderunit * core);
int   mali_core_renderunit_map_registers(struct mali_core_renderunit *core);
void  mali_core_renderunit_unmap_registers(struct mali_core_renderunit *core);
int   mali_core_renderunit_irq_handler_add(struct mali_core_renderunit *core);
mali_core_renderunit * mali_core_renderunit_get_mali_core_nr(mali_core_subsystem *subsys, u32 mali_core_nr);

int mali_core_subsystem_init(struct mali_core_subsystem * new_subsys);
#if USING_MMU
void mali_core_subsystem_attach_mmu(mali_core_subsystem* subsys);
#endif
int mali_core_subsystem_register_renderunit(struct mali_core_subsystem * subsys, struct mali_core_renderunit * core);
int mali_core_subsystem_system_info_fill(mali_core_subsystem* subsys, _mali_system_info* info);
void mali_core_subsystem_cleanup(struct mali_core_subsystem * subsys);
#if USING_MMU
void mali_core_subsystem_broadcast_notification(struct mali_core_subsystem * subsys, mali_core_notification_message message, u32 data);
#endif
void mali_core_session_begin(mali_core_session *session);
void mali_core_session_close(mali_core_session * session);
int mali_core_session_add_job(mali_core_session * session, mali_core_job *job, mali_core_job **job_return);
u32 mali_core_hang_check_timeout_get(void);

_mali_osk_errcode_t mali_core_subsystem_ioctl_start_job(mali_core_session * session, void *job_data);
_mali_osk_errcode_t mali_core_subsystem_ioctl_number_of_cores_get(mali_core_session * session, u32 *number_of_cores);
_mali_osk_errcode_t mali_core_subsystem_ioctl_core_version_get(mali_core_session * session, _mali_core_version *version);
_mali_osk_errcode_t mali_core_subsystem_ioctl_suspend_response(mali_core_session * session, void* argument);
void mali_core_subsystem_ioctl_abort_job(mali_core_session * session, u32 id);

#if USING_MALI_PMM
_mali_osk_errcode_t mali_core_subsystem_signal_power_down(mali_core_subsystem *subsys, u32 mali_core_nr, mali_bool immediate_only);
_mali_osk_errcode_t mali_core_subsystem_signal_power_up(mali_core_subsystem *subsys, u32 mali_core_nr, mali_bool queue_only);
#endif

#if MALI_STATE_TRACKING
u32 mali_core_renderunit_dump_state(mali_core_subsystem* subsystem, char *buf, u32 size);
#endif

#endif /* __MALI_RENDERCORE_H__ */
