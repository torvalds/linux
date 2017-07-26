#ifndef _MEDUSA_CONSTANTS_H
#define _MEDUSA_CONSTANTS_H

#include <linux/medusa/l4/comm.h>

#pragma GCC optimize ("Og")

/* these constants may be used by both internal kernel data structures,
 * and a communication protocol. if you alter them, you'll break the
 * comm protocol, and build of some l4 servers might fail.
 *
 * moreover, if you change the medusa_answer_t, the world will die in pain.
 */

/* elementary data types for attributes */
#define MED_END		MED_COMM_TYPE_END	/* end of attribute list */
#define MED_UNSIGNED	MED_COMM_TYPE_UNSIGNED	/* unsigned integer attr */
#define MED_SIGNED	MED_COMM_TYPE_SIGNED	/* signed integer attr */
#define MED_STRING	MED_COMM_TYPE_STRING	/* string attr */
#define MED_BITMAP	MED_COMM_TYPE_BITMAP	/* bitmap attr */

#define MED_KEY		MED_COMM_TYPE_PRIMARY_KEY /* attribute is used to lookup kobject */
#define MED_RO		MED_COMM_TYPE_READ_ONLY	/* attribute is read-only */

/* string lengths in various structures */
#define MEDUSA_ATTRNAME_MAX	 MEDUSA_COMM_ATTRNAME_MAX
#define MEDUSA_KCLASSNAME_MAX	 MEDUSA_COMM_KCLASSNAME_MAX
#define MEDUSA_EVNAME_MAX	 MEDUSA_COMM_EVNAME_MAX
#define MEDUSA_ACCNAME_MAX	MEDUSA_EVNAME_MAX
#define MEDUSA_SERVERNAME_MAX	128

/* answer codes */

typedef enum {
	MED_ERR =	-1,	/* error */
	MED_YES =	 0,	/* permit the operation */
	MED_NO =	 1,	/* forbid the operation */
	MED_SKIP =	 2,	/* forbid the operation, but return success */
	MED_OK =	 3	/* permit the operation, but proceed with
				   standard system permission check if any */
} medusa_answer_t;

#endif

