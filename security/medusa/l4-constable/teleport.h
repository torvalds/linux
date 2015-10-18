#ifndef _MEDUSA_TELEPORT_H
#define _MEDUSA_TELEPORT_H

#include <linux/medusa/l4/comm.h>

typedef enum {
	tp_NOP,		/* do nothing */
	tp_PUT16,	/* put 16-bit constant */
	tp_PUT32,	/* put 32-bit constant */
	tp_PUTPtr,	/* put Pointer value *JK */
	tp_CUTNPASTE,	/* put the memory region */
	tp_PUTATTRS,	/* put attributes */
	tp_PUTKCLASS,	/* put kclass (without attrs) and assign it a number */
	tp_PUTEVTYPE,	/* put evtype (...) ... */

	tp_HALT,	/* end of the routine */
} teleport_opcode_t;

typedef enum {
	tpc_FETCH,	/* instruction fetch (and decode as well) */
	tpc_EXECUTE,	/* instruction execution */
	tpc_HALT,	/* does nothing */
} teleport_cycle_t;

typedef struct {
	int opcode;
	union {
		struct {
			void * data[2];
		} nop;
		struct {
			u_int16_t what;
		} put16;
		struct {
			u_int32_t what;
		} put32;
		struct {
			MCPptr_t what;
		} putPtr;
		struct {
			unsigned char * from;
			unsigned int count;
		} cutnpaste;
		struct {
			struct medusa_attribute_s * attrlist;
		} putattrs;
		struct {
			struct medusa_kclass_s * kclassdef;
		} putkclass;
		struct {
			struct medusa_evtype_s * evtypedef;
		} putevtype;
	} args;
} teleport_insn_t;

typedef struct {
	/* instruction to execute */
	teleport_insn_t * ip;
	teleport_cycle_t cycle;

	/* registers of the processor */
	unsigned char * data_to_user;
	size_t remaining;
	union {
		struct {
			int current_attr;
			struct medusa_comm_attribute_s attr;
		} putattrs;
		struct {
			struct medusa_comm_kclass_s cl;
		} putkclass;
		struct {
			struct medusa_comm_evtype_s ev;
		} putevtype;
	} u;
} teleport_t;

extern void teleport_reset(teleport_t * teleport, teleport_insn_t * addr,
		ssize_t (*to_user)(void * from, size_t len));
extern ssize_t teleport_cycle(teleport_t * teleport,
		size_t userlimit);
#endif
