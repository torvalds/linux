/*
 * Copyright (C) 2004 Jeff Dike (jdike@addtoit.com)
 * Copyright 2003 PathScale, Inc.
 * Licensed under the GPL
 */

#ifndef __VM_FLAGS_X86_64_H
#define __VM_FLAGS_X86_64_H

#define __VM_DATA_DEFAULT_FLAGS	(VM_READ | VM_WRITE | VM_EXEC | \
				 VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)
#define __VM_STACK_FLAGS 	(VM_GROWSDOWN | VM_READ | VM_WRITE | \
				 VM_EXEC | VM_MAYREAD | VM_MAYWRITE | \
				 VM_MAYEXEC)

extern unsigned long vm_stack_flags, vm_stack_flags32;
extern unsigned long vm_data_default_flags, vm_data_default_flags32;
extern unsigned long vm_force_exec32;

#ifdef TIF_IA32
#define VM_DATA_DEFAULT_FLAGS \
	(test_thread_flag(TIF_IA32) ? vm_data_default_flags32 : \
	  vm_data_default_flags)

#define VM_STACK_DEFAULT_FLAGS \
	(test_thread_flag(TIF_IA32) ? vm_stack_flags32 : vm_stack_flags)
#endif

#define VM_DATA_DEFAULT_FLAGS vm_data_default_flags

#define VM_STACK_DEFAULT_FLAGS vm_stack_flags

#endif
