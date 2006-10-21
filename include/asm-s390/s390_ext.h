#ifndef _S390_EXTINT_H
#define _S390_EXTINT_H

/*
 *  include/asm-s390/s390_ext.h
 *
 *  S390 version
 *    Copyright (C) 1999,2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Holger Smolinski (Holger.Smolinski@de.ibm.com),
 *               Martin Schwidefsky (schwidefsky@de.ibm.com)
 */

typedef void (*ext_int_handler_t)(__u16 code);

/*
 * Warning: if you change ext_int_info_t you have to change the
 * external interrupt handler in entry.S too.
 */ 
typedef struct ext_int_info_t {
	struct ext_int_info_t *next;
	ext_int_handler_t handler;
	__u16 code;
} __attribute__ ((packed)) ext_int_info_t;

extern ext_int_info_t *ext_int_hash[];

int register_external_interrupt(__u16 code, ext_int_handler_t handler);
int register_early_external_interrupt(__u16 code, ext_int_handler_t handler,
				      ext_int_info_t *info);
int unregister_external_interrupt(__u16 code, ext_int_handler_t handler);
int unregister_early_external_interrupt(__u16 code, ext_int_handler_t handler,
					ext_int_info_t *info);

#endif
