/* Public domain. */

#ifndef _LINUX_MM_TYPES_H
#define _LINUX_MM_TYPES_H

#include <linux/workqueue.h>
#include <linux/completion.h>
#include <linux/rwsem.h>

#include <uvm/uvm_extern.h>

#define VM_FAULT_NOPAGE		1
#define VM_FAULT_SIGBUS		2
#define VM_FAULT_RETRY		3
#define VM_FAULT_OOM		4

#endif
