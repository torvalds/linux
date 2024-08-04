/*
 *  include/linux/printk_self.h
 *
 *  Copyright (C) 2024  Qihong Ren
 *  A learning the kernel tool for tracking source code execution
 
 *  Steps:

 * 1. Add print logs in functions like: pr_info_self("%s", name);
 
 * 2. Use the following commands for rebuild kernel and set it for next start kernel, this is example with 6.9.0:
	 'make -j16'
	 'make -j8 modules_install' 
	 'make -j8 install' 
	 'mkinitramfs -o /boot/initrd.img-6.9.0' 
	 'update-initramfs -c -k 6.9.0' 
	 'reboot'
	 
 * 3. Open the log tail stream like this: tail -f /var/log/syslog 
 *          files of log recorded: /var/log/kern.log or /var/log/syslog or /var/log/dmesg
 
 * 4. start log print in files:
          touch LOG_DEFAULT( with default level, of course others level can work) 
          after this, may be no print logs, because some functions need OS layer operations, like: mkdir, touch files, wget url, etc...
 *                            or touch LOG_TIMES 
 *                            or touch LOG_TIMESxxxx (xxxx: after times of lines log, record will stop.)
 *                            or etc (infer fs/open.c do_sys_openat2)

 * 5. You can create file or create dirs or wget something etc if you add pr_info_self macro in the functions

 * 6. Then you will get logs print in step 3 terminal
 
 * 5. stop log print in files:
 	touch LOG_STOP

 * Note: 
 	1.Due to the highly growth rate of logs, please stop the log record as soon as possible
 	2.You can modify filename in: static const char *const record_file_names[PRINT_LOG_STATE_MAX]
 *
 * If you want to record log from kernel start please add the similar codes into codes:
 *	GlobalLogParametersInit(PRINT_LOG_STATE_TIMES, 100);
 *	pr_info_self("start_kernel(void)");
 *
 * If you have any questions can contact <77683962@qq.com>

 * enjoys.
 */


#ifndef __KERNEL_PRINTK_SELF__
#define __KERNEL_PRINTK_SELF__

#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/stdarg.h>
#include <linux/init.h>
#include <linux/kern_levels.h>
#include <linux/linkage.h>
#include <linux/sched.h>
#include <linux/ratelimit_types.h>
#include <linux/once_lite.h>

typedef enum 
{
	PRINT_LOG_STATE_LESS = 0,
	PRINT_LOG_STATE_DEFAULT,
	PRINT_LOG_STATE_MORE,
	PRINT_LOG_STATE_PROCESS,
	PRINT_LOG_STATE_TIMES,
	PRINT_LOG_STATE_STOP,
	PRINT_LOG_STATE_MAX
}PRINT_LOG_STATE_EN;

/*infer from static const int sig_map[MAXMAPPED_SIG] = {*/
static const char *const record_file_names[PRINT_LOG_STATE_MAX] = {
        "LOG_LESS",
        "LOG_DEFAULT",
        "LOG_MORE",
        "LOG_PROCESS",
        "LOG_TIMES",
        "LOG_STOP",
};

extern int iGlobalLogPrintLevel;
extern int iGlobalLogPrintTimes; 

void GlobalLogParametersInit(int iLevel, int iTimes);

/**
 * Infer pr_info from include/linux/printk.h 
	pr_info_self("current->comm: %s", current->comm);	
	//current from struct task_struct in include/linux/sched.h
 */
#define pr_info_self(fmt, ...) \
({                                                                      \
	if (iGlobalLogPrintLevel == PRINT_LOG_STATE_DEFAULT) \
	    printk(KERN_INFO "[%s %s %d %d %s %s] "pr_fmt(fmt), __FILE__, __FUNCTION__, __LINE__, current->pid, current->comm, record_file_names[PRINT_LOG_STATE_DEFAULT], ##__VA_ARGS__); \
	\
	else if (iGlobalLogPrintLevel == PRINT_LOG_STATE_LESS) \
	    printk(KERN_INFO "[%s %s %d %s] "pr_fmt(fmt), __FILE__, __FUNCTION__, __LINE__, record_file_names[PRINT_LOG_STATE_LESS], ##__VA_ARGS__); \
	\
	else if (iGlobalLogPrintLevel == PRINT_LOG_STATE_PROCESS) \
	    printk(KERN_INFO "[%s %s %d %d %s %s] "pr_fmt(fmt), __FILE__, __FUNCTION__, __LINE__, current->pid, current->comm, record_file_names[PRINT_LOG_STATE_PROCESS], ##__VA_ARGS__); \
	\
	else if (iGlobalLogPrintLevel == PRINT_LOG_STATE_MORE) \
	    printk(KERN_INFO "[%s %s %d %d %d %s %s] "pr_fmt(fmt), __FILE__, __FUNCTION__, __LINE__, current->tgid, current->pid, current->comm, record_file_names[PRINT_LOG_STATE_MORE], ##__VA_ARGS__); \
	\
	else if (iGlobalLogPrintLevel == PRINT_LOG_STATE_TIMES && iGlobalLogPrintTimes > 0) \
	{  \
	    printk(KERN_INFO "[%s %s %d %s: %d ] "pr_fmt(fmt), __FILE__, __FUNCTION__, __LINE__, record_file_names[PRINT_LOG_STATE_TIMES], iGlobalLogPrintTimes, ##__VA_ARGS__); \
	    iGlobalLogPrintTimes--; \
	} \
})


#endif

