
/******************************************************************************
 *
 * Name: acobject.h - Definition of union acpi_operand_object    (Internal object only)
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2006, R. Byron Moore
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 */

#ifndef _ACOBJECT_H
#define _ACOBJECT_H

/*
 * The union acpi_operand_object    is used to pass AML operands from the dispatcher
 * to the interpreter, and to keep track of the various handlers such as
 * address space handlers and notify handlers.  The object is a constant
 * size in order to allow it to be cached and reused.
 */

/*******************************************************************************
 *
 * Common Descriptors
 *
 ******************************************************************************/

/*
 * Common area for all objects.
 *
 * data_type is used to differentiate between internal descriptors, and MUST
 * be the first byte in this structure.
 */
#define ACPI_OBJECT_COMMON_HEADER   /* SIZE/ALIGNMENT: 32 bits, one ptr plus trailing 8-bit flag */\
	u8                                      descriptor;         /* To differentiate various internal objs */\
	u8                                      type;               /* acpi_object_type */\
	u16                                     reference_count;    /* For object deletion management */\
	union acpi_operand_object               *next_object;       /* Objects linked to parent NS node */\
	u8                                      flags;

/* Values for flag byte above */

#define AOPOBJ_AML_CONSTANT         0x01
#define AOPOBJ_STATIC_POINTER       0x02
#define AOPOBJ_DATA_VALID           0x04
#define AOPOBJ_OBJECT_INITIALIZED   0x08
#define AOPOBJ_SETUP_COMPLETE       0x10
#define AOPOBJ_SINGLE_DATUM         0x20

/*
 * Common bitfield for the field objects
 * "Field Datum"  -- a datum from the actual field object
 * "Buffer Datum" -- a datum from a user buffer, read from or to be written to the field
 */
#define ACPI_COMMON_FIELD_INFO      /* SIZE/ALIGNMENT: 24 bits + three 32-bit values */\
	u8                                      field_flags;        /* Access, update, and lock bits */\
	u8                                      attribute;          /* From access_as keyword */\
	u8                                      access_byte_width;  /* Read/Write size in bytes */\
	u32                                     bit_length;         /* Length of field in bits */\
	u32                                     base_byte_offset;   /* Byte offset within containing object */\
	u8                                      start_field_bit_offset;/* Bit offset within first field datum (0-63) */\
	u8                                      access_bit_width;   /* Read/Write size in bits (8-64) */\
	u32                                     value;              /* Value to store into the Bank or Index register */\
	struct acpi_namespace_node              *node;	/* Link back to parent node */

/*
 * Fields common to both Strings and Buffers
 */
#define ACPI_COMMON_BUFFER_INFO \
	u32                                     length;

/*
 * Common fields for objects that support ASL notifications
 */
#define ACPI_COMMON_NOTIFY_INFO \
	union acpi_operand_object               *system_notify;     /* Handler for system notifies */\
	union acpi_operand_object               *device_notify;     /* Handler for driver notifies */\
	union acpi_operand_object               *handler;	/* Handler for Address space */

/******************************************************************************
 *
 * Basic data types
 *
 *****************************************************************************/

struct acpi_object_common {
ACPI_OBJECT_COMMON_HEADER};

struct acpi_object_integer {
	ACPI_OBJECT_COMMON_HEADER acpi_integer value;
};

/*
 * Note: The String and Buffer object must be identical through the Pointer
 * element.  There is code that depends on this.
 */
struct acpi_object_string {	/* Null terminated, ASCII characters only */
	ACPI_OBJECT_COMMON_HEADER ACPI_COMMON_BUFFER_INFO char *pointer;	/* String in AML stream or allocated string */
};

struct acpi_object_buffer {
	ACPI_OBJECT_COMMON_HEADER ACPI_COMMON_BUFFER_INFO u8 * pointer;	/* Buffer in AML stream or allocated buffer */
	struct acpi_namespace_node *node;	/* Link back to parent node */
	u8 *aml_start;
	u32 aml_length;
};

struct acpi_object_package {
	ACPI_OBJECT_COMMON_HEADER u32 count;	/* # of elements in package */
	u32 aml_length;
	u8 *aml_start;
	struct acpi_namespace_node *node;	/* Link back to parent node */
	union acpi_operand_object **elements;	/* Array of pointers to acpi_objects */
};

/******************************************************************************
 *
 * Complex data types
 *
 *****************************************************************************/

struct acpi_object_event {
	ACPI_OBJECT_COMMON_HEADER void *semaphore;
};

#define ACPI_INFINITE_CONCURRENCY   0xFF

typedef
acpi_status(*ACPI_INTERNAL_METHOD) (struct acpi_walk_state * walk_state);

struct acpi_object_method {
	ACPI_OBJECT_COMMON_HEADER u8 method_flags;
	u8 param_count;
	u32 aml_length;
	void *semaphore;
	u8 *aml_start;
	ACPI_INTERNAL_METHOD implementation;
	u8 concurrency;
	u8 thread_count;
	acpi_owner_id owner_id;
};

struct acpi_object_mutex {
	ACPI_OBJECT_COMMON_HEADER u8 sync_level;	/* 0-15, specified in Mutex() call */
	u16 acquisition_depth;	/* Allow multiple Acquires, same thread */
	struct acpi_thread_state *owner_thread;	/* Current owner of the mutex */
	void *semaphore;	/* Actual OS synchronization object */
	union acpi_operand_object *prev;	/* Link for list of acquired mutexes */
	union acpi_operand_object *next;	/* Link for list of acquired mutexes */
	struct acpi_namespace_node *node;	/* Containing namespace node */
	u8 original_sync_level;	/* Owner's original sync level (0-15) */
};

struct acpi_object_region {
	ACPI_OBJECT_COMMON_HEADER u8 space_id;
	union acpi_operand_object *handler;	/* Handler for region access */
	struct acpi_namespace_node *node;	/* Containing namespace node */
	union acpi_operand_object *next;
	u32 length;
	acpi_physical_address address;
};

/******************************************************************************
 *
 * Objects that can be notified.  All share a common notify_info area.
 *
 *****************************************************************************/

struct acpi_object_notify_common {	/* COMMON NOTIFY for POWER, PROCESSOR, DEVICE, and THERMAL */
ACPI_OBJECT_COMMON_HEADER ACPI_COMMON_NOTIFY_INFO};

struct acpi_object_device {
	ACPI_OBJECT_COMMON_HEADER
	    ACPI_COMMON_NOTIFY_INFO struct acpi_gpe_block_info *gpe_block;
};

struct acpi_object_power_resource {
	ACPI_OBJECT_COMMON_HEADER ACPI_COMMON_NOTIFY_INFO u32 system_level;
	u32 resource_order;
};

struct acpi_object_processor {
	ACPI_OBJECT_COMMON_HEADER ACPI_COMMON_NOTIFY_INFO u32 proc_id;
	u32 length;
	acpi_io_address address;
};

struct acpi_object_thermal_zone {
ACPI_OBJECT_COMMON_HEADER ACPI_COMMON_NOTIFY_INFO};

/******************************************************************************
 *
 * Fields.  All share a common header/info field.
 *
 *****************************************************************************/

struct acpi_object_field_common {	/* COMMON FIELD (for BUFFER, REGION, BANK, and INDEX fields) */
	ACPI_OBJECT_COMMON_HEADER ACPI_COMMON_FIELD_INFO union acpi_operand_object *region_obj;	/* Containing Operation Region object */
	/* (REGION/BANK fields only) */
};

struct acpi_object_region_field {
	ACPI_OBJECT_COMMON_HEADER ACPI_COMMON_FIELD_INFO union acpi_operand_object *region_obj;	/* Containing op_region object */
};

struct acpi_object_bank_field {
	ACPI_OBJECT_COMMON_HEADER ACPI_COMMON_FIELD_INFO union acpi_operand_object *region_obj;	/* Containing op_region object */
	union acpi_operand_object *bank_obj;	/* bank_select Register object */
};

struct acpi_object_index_field {
	ACPI_OBJECT_COMMON_HEADER ACPI_COMMON_FIELD_INFO
	    /*
	     * No "region_obj" pointer needed since the Index and Data registers
	     * are each field definitions unto themselves.
	     */
	union acpi_operand_object *index_obj;	/* Index register */
	union acpi_operand_object *data_obj;	/* Data register */
};

/* The buffer_field is different in that it is part of a Buffer, not an op_region */

struct acpi_object_buffer_field {
	ACPI_OBJECT_COMMON_HEADER ACPI_COMMON_FIELD_INFO union acpi_operand_object *buffer_obj;	/* Containing Buffer object */
};

/******************************************************************************
 *
 * Objects for handlers
 *
 *****************************************************************************/

struct acpi_object_notify_handler {
	ACPI_OBJECT_COMMON_HEADER struct acpi_namespace_node *node;	/* Parent device */
	acpi_notify_handler handler;
	void *context;
};

/* Flags for address handler */

#define ACPI_ADDR_HANDLER_DEFAULT_INSTALLED  0x1

struct acpi_object_addr_handler {
	ACPI_OBJECT_COMMON_HEADER u8 space_id;
	u16 hflags;
	acpi_adr_space_handler handler;
	struct acpi_namespace_node *node;	/* Parent device */
	void *context;
	acpi_adr_space_setup setup;
	union acpi_operand_object *region_list;	/* regions using this handler */
	union acpi_operand_object *next;
};

/******************************************************************************
 *
 * Special internal objects
 *
 *****************************************************************************/

/*
 * The Reference object type is used for these opcodes:
 * Arg[0-6], Local[0-7], index_op, name_op, zero_op, one_op, ones_op, debug_op
 */
struct acpi_object_reference {
	ACPI_OBJECT_COMMON_HEADER u8 target_type;	/* Used for index_op */
	u16 opcode;
	u32 offset;		/* Used for arg_op, local_op, and index_op */
	void *object;		/* name_op=>HANDLE to obj, index_op=>union acpi_operand_object  */
	struct acpi_namespace_node *node;
	union acpi_operand_object **where;
};

/*
 * Extra object is used as additional storage for types that
 * have AML code in their declarations (term_args) that must be
 * evaluated at run time.
 *
 * Currently: Region and field_unit types
 */
struct acpi_object_extra {
	ACPI_OBJECT_COMMON_HEADER u8 byte_fill1;
	u16 word_fill1;
	u32 aml_length;
	u8 *aml_start;
	struct acpi_namespace_node *method_REG;	/* _REG method for this region (if any) */
	void *region_context;	/* Region-specific data */
};

/* Additional data that can be attached to namespace nodes */

struct acpi_object_data {
	ACPI_OBJECT_COMMON_HEADER acpi_object_handler handler;
	void *pointer;
};

/* Structure used when objects are cached for reuse */

struct acpi_object_cache_list {
	ACPI_OBJECT_COMMON_HEADER union acpi_operand_object *next;	/* Link for object cache and internal lists */
};

/******************************************************************************
 *
 * union acpi_operand_object Descriptor - a giant union of all of the above
 *
 *****************************************************************************/

union acpi_operand_object {
	struct acpi_object_common common;
	struct acpi_object_integer integer;
	struct acpi_object_string string;
	struct acpi_object_buffer buffer;
	struct acpi_object_package package;
	struct acpi_object_event event;
	struct acpi_object_method method;
	struct acpi_object_mutex mutex;
	struct acpi_object_region region;
	struct acpi_object_notify_common common_notify;
	struct acpi_object_device device;
	struct acpi_object_power_resource power_resource;
	struct acpi_object_processor processor;
	struct acpi_object_thermal_zone thermal_zone;
	struct acpi_object_field_common common_field;
	struct acpi_object_region_field field;
	struct acpi_object_buffer_field buffer_field;
	struct acpi_object_bank_field bank_field;
	struct acpi_object_index_field index_field;
	struct acpi_object_notify_handler notify;
	struct acpi_object_addr_handler address_space;
	struct acpi_object_reference reference;
	struct acpi_object_extra extra;
	struct acpi_object_data data;
	struct acpi_object_cache_list cache;
};

/******************************************************************************
 *
 * union acpi_descriptor - objects that share a common descriptor identifier
 *
 *****************************************************************************/

/* Object descriptor types */

#define ACPI_DESC_TYPE_CACHED           0x01	/* Used only when object is cached */
#define ACPI_DESC_TYPE_STATE            0x02
#define ACPI_DESC_TYPE_STATE_UPDATE     0x03
#define ACPI_DESC_TYPE_STATE_PACKAGE    0x04
#define ACPI_DESC_TYPE_STATE_CONTROL    0x05
#define ACPI_DESC_TYPE_STATE_RPSCOPE    0x06
#define ACPI_DESC_TYPE_STATE_PSCOPE     0x07
#define ACPI_DESC_TYPE_STATE_WSCOPE     0x08
#define ACPI_DESC_TYPE_STATE_RESULT     0x09
#define ACPI_DESC_TYPE_STATE_NOTIFY     0x0A
#define ACPI_DESC_TYPE_STATE_THREAD     0x0B
#define ACPI_DESC_TYPE_WALK             0x0C
#define ACPI_DESC_TYPE_PARSER           0x0D
#define ACPI_DESC_TYPE_OPERAND          0x0E
#define ACPI_DESC_TYPE_NAMED            0x0F
#define ACPI_DESC_TYPE_MAX              0x0F

union acpi_descriptor {
	u8 descriptor_id;	/* To differentiate various internal objs */
	union acpi_operand_object object;
	struct acpi_namespace_node node;
	union acpi_parse_object op;
};

#endif				/* _ACOBJECT_H */
