#include <linux/medusa/l3/registry.h>

struct ipc_kobject {	
	unsigned int sid;
	unsigned int sclass;
	MEDUSA_OBJECT_VARS;
};
extern MED_DECLARE_KCLASSOF(ipc_kobject);

