/* comm.c, (C) 2002 Milan Pikula <www@terminus.sk>
 *
 */
#include <linux/medusa/l3/arch.h>
#include <linux/medusa/l3/registry.h>
#include <linux/medusa/l3/server.h>
#include "l3_internals.h"

medusa_answer_t med_decide(struct medusa_evtype_s * evtype, void * event, void * o1, void * o2)
{
	int retval;
	struct medusa_authserver_s * authserver;

	if (ARCH_CANNOT_DECIDE(evtype))
		return MED_OK;

	MED_LOCK_W(registry_lock);
#ifdef CONFIG_MEDUSA_PROFILING
	evtype->arg_kclass[0]->l2_to_l4++;
	evtype->arg_kclass[1]->l2_to_l4++;
	evtype->l2_to_l4++;
#endif
	authserver = med_get_authserver();
	if (!authserver) {
		if (evtype->arg_kclass[0]->unmonitor)
			evtype->arg_kclass[0]->unmonitor((struct medusa_kobject_s *) o1);
		if (evtype->arg_kclass[1]->unmonitor)
			evtype->arg_kclass[1]->unmonitor((struct medusa_kobject_s *) o2);
		MED_UNLOCK_W(registry_lock);
		return MED_OK;
	}
	MED_UNLOCK_W(registry_lock);

	((struct medusa_event_s *)event)->evtype_id = evtype;
	((struct medusa_kobject_s *)o1)->kclass_id = evtype->arg_kclass[0];
	((struct medusa_kobject_s *)o2)->kclass_id = evtype->arg_kclass[1];
	retval = authserver->decide(event, o1, o2);
#ifdef CONFIG_MEDUSA_PROFILING
	if (retval != MED_ERR) {
		MED_LOCK_W(registry_lock);
		evtype->l4_to_l2++;
		MED_UNLOCK_W(registry_lock);
	}
#endif
	med_put_authserver(authserver);
	return retval;
}
