/* $Id: firmware_core_dump.h,v 1.1 2007/09/09 13:29:30 peek Exp $ */

#ifndef _FIRMWARE_CORE_DUMP_H_
#define _FIRMWARE_CORE_DUMP_H_

void* core_dump_start(int objId, int errCode, int expected_size);
int   core_dump_append(void *ctx, void *data, size_t len);
int   core_dump_abort(void **ctx);
int   core_dump_complete(void **ctx);

#endif /* _FIRMWARE_CORE_DUMP_H_ */
