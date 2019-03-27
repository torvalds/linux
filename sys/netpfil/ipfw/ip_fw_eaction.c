/*-
 * Copyright (c) 2016-2017 Yandex LLC
 * Copyright (c) 2016-2017 Andrey V. Elsukov <ae@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/hash.h>
#include <sys/lock.h>
#include <sys/rwlock.h>
#include <sys/rmlock.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/queue.h>

#include <net/if.h>	/* ip_fw.h requires IFNAMSIZ */
#include <net/pfil.h>
#include <netinet/in.h>
#include <netinet/ip_var.h>	/* struct ipfw_rule_ref */
#include <netinet/ip_fw.h>

#include <netpfil/ipfw/ip_fw_private.h>

#include "opt_ipfw.h"

/*
 * External actions support for ipfw.
 *
 * This code provides KPI for implementing loadable modules, that
 * can provide handlers for external action opcodes in the ipfw's
 * rules.
 * Module should implement opcode handler with type ipfw_eaction_t.
 * This handler will be called by ipfw_chk() function when
 * O_EXTERNAL_ACTION opcode is matched. The handler must return
 * value used as return value in ipfw_chk(), i.e. IP_FW_PASS,
 * IP_FW_DENY (see ip_fw_private.h).
 * Also the last argument must be set by handler. If it is zero,
 * the search continues to the next rule. If it has non zero value,
 * the search terminates.
 *
 * The module that implements external action should register its
 * handler and name with ipfw_add_eaction() function.
 * This function will return eaction_id, that can be used by module.
 *
 * It is possible to pass some additional information to external
 * action handler using O_EXTERNAL_INSTANCE and O_EXTERNAL_DATA opcodes.
 * Such opcodes should be next after the O_EXTERNAL_ACTION opcode.
 * For the O_EXTERNAL_INSTANCE opcode the cmd->arg1 contains index of named
 * object related to an instance of external action.
 * For the O_EXTERNAL_DATA opcode the cmd contains the data that can be used
 * by external action handler without needing to create named instance.
 *
 * In case when eaction module uses named instances, it should register
 * opcode rewriting routines for O_EXTERNAL_INSTANCE opcode. The
 * classifier callback can look back into O_EXTERNAL_ACTION opcode (it
 * must be in the (ipfw_insn *)(cmd - 1)). By arg1 from O_EXTERNAL_ACTION
 * it can deteremine eaction_id and compare it with its own.
 * The macro IPFW_TLV_EACTION_NAME(eaction_id) can be used to deteremine
 * the type of named_object related to external action instance.
 *
 * On module unload handler should be deregistered with ipfw_del_eaction()
 * function using known eaction_id.
 */

struct eaction_obj {
	struct named_object	no;
	ipfw_eaction_t		*handler;
	char			name[64];
};

#define	EACTION_OBJ(ch, cmd)			\
    ((struct eaction_obj *)SRV_OBJECT((ch), (cmd)->arg1))

#if 0
#define	EACTION_DEBUG(fmt, ...)	do {			\
	printf("%s: " fmt "\n", __func__, ## __VA_ARGS__);	\
} while (0)
#else
#define	EACTION_DEBUG(fmt, ...)
#endif

const char *default_eaction_typename = "drop";
static int
default_eaction(struct ip_fw_chain *ch, struct ip_fw_args *args,
    ipfw_insn *cmd, int *done)
{

	*done = 1; /* terminate the search */
	return (IP_FW_DENY);
}

/*
 * Opcode rewriting callbacks.
 */
static int
eaction_classify(ipfw_insn *cmd, uint16_t *puidx, uint8_t *ptype)
{

	EACTION_DEBUG("opcode %d, arg1 %d", cmd->opcode, cmd->arg1);
	*puidx = cmd->arg1;
	*ptype = 0;
	return (0);
}

static void
eaction_update(ipfw_insn *cmd, uint16_t idx)
{

	cmd->arg1 = idx;
	EACTION_DEBUG("opcode %d, arg1 -> %d", cmd->opcode, cmd->arg1);
}

static int
eaction_findbyname(struct ip_fw_chain *ch, struct tid_info *ti,
    struct named_object **pno)
{
	ipfw_obj_ntlv *ntlv;

	if (ti->tlvs == NULL)
		return (EINVAL);

	/* Search ntlv in the buffer provided by user */
	ntlv = ipfw_find_name_tlv_type(ti->tlvs, ti->tlen, ti->uidx,
	    IPFW_TLV_EACTION);
	if (ntlv == NULL)
		return (EINVAL);
	EACTION_DEBUG("name %s, uidx %u, type %u", ntlv->name,
	    ti->uidx, ti->type);
	/*
	 * Search named object with corresponding name.
	 * Since eaction objects are global - ignore the set value
	 * and use zero instead.
	 */
	*pno = ipfw_objhash_lookup_name_type(CHAIN_TO_SRV(ch),
	    0, IPFW_TLV_EACTION, ntlv->name);
	if (*pno == NULL)
		return (ESRCH);
	return (0);
}

static struct named_object *
eaction_findbykidx(struct ip_fw_chain *ch, uint16_t idx)
{

	EACTION_DEBUG("kidx %u", idx);
	return (ipfw_objhash_lookup_kidx(CHAIN_TO_SRV(ch), idx));
}

static struct opcode_obj_rewrite eaction_opcodes[] = {
	{
		.opcode = O_EXTERNAL_ACTION,
		.etlv = IPFW_TLV_EACTION,
		.classifier = eaction_classify,
		.update = eaction_update,
		.find_byname = eaction_findbyname,
		.find_bykidx = eaction_findbykidx,
	},
};

static int
create_eaction_obj(struct ip_fw_chain *ch, ipfw_eaction_t handler,
    const char *name, uint16_t *eaction_id)
{
	struct namedobj_instance *ni;
	struct eaction_obj *obj;

	IPFW_UH_UNLOCK_ASSERT(ch);

	ni = CHAIN_TO_SRV(ch);
	obj = malloc(sizeof(*obj), M_IPFW, M_WAITOK | M_ZERO);
	obj->no.name = obj->name;
	obj->no.etlv = IPFW_TLV_EACTION;
	obj->handler = handler;
	strlcpy(obj->name, name, sizeof(obj->name));

	IPFW_UH_WLOCK(ch);
	if (ipfw_objhash_lookup_name_type(ni, 0, IPFW_TLV_EACTION,
	    name) != NULL) {
		/*
		 * Object is already created.
		 * We don't allow eactions with the same name.
		 */
		IPFW_UH_WUNLOCK(ch);
		free(obj, M_IPFW);
		EACTION_DEBUG("External action with typename "
		    "'%s' already exists", name);
		return (EEXIST);
	}
	if (ipfw_objhash_alloc_idx(ni, &obj->no.kidx) != 0) {
		IPFW_UH_WUNLOCK(ch);
		free(obj, M_IPFW);
		EACTION_DEBUG("alloc_idx failed");
		return (ENOSPC);
	}
	ipfw_objhash_add(ni, &obj->no);
	IPFW_WLOCK(ch);
	SRV_OBJECT(ch, obj->no.kidx) = obj;
	IPFW_WUNLOCK(ch);
	obj->no.refcnt++;
	IPFW_UH_WUNLOCK(ch);

	if (eaction_id != NULL)
		*eaction_id = obj->no.kidx;
	return (0);
}

static void
destroy_eaction_obj(struct ip_fw_chain *ch, struct named_object *no)
{
	struct namedobj_instance *ni;
	struct eaction_obj *obj;

	IPFW_UH_WLOCK_ASSERT(ch);

	ni = CHAIN_TO_SRV(ch);
	IPFW_WLOCK(ch);
	obj = SRV_OBJECT(ch, no->kidx);
	SRV_OBJECT(ch, no->kidx) = NULL;
	IPFW_WUNLOCK(ch);
	ipfw_objhash_del(ni, no);
	ipfw_objhash_free_idx(ni, no->kidx);
	free(obj, M_IPFW);
}

/*
 * Resets all eaction opcodes to default handlers.
 */
static void
reset_eaction_rules(struct ip_fw_chain *ch, uint16_t eaction_id,
    uint16_t instance_id, bool reset_rules)
{
	struct named_object *no;
	int i;

	IPFW_UH_WLOCK_ASSERT(ch);

	no = ipfw_objhash_lookup_name_type(CHAIN_TO_SRV(ch), 0,
	    IPFW_TLV_EACTION, default_eaction_typename);
	if (no == NULL)
		panic("Default external action handler is not found");
	if (eaction_id == no->kidx)
		panic("Wrong eaction_id");

	EACTION_DEBUG("Going to replace id %u with %u", eaction_id, no->kidx);
	IPFW_WLOCK(ch);
	/*
	 * Reset eaction objects only if it is referenced by rules.
	 * But always reset objects for orphaned dynamic states.
	 */
	if (reset_rules) {
		for (i = 0; i < ch->n_rules; i++) {
			/*
			 * Refcount on the original object will be just
			 * ignored on destroy. Refcount on default_eaction
			 * will be decremented on rule deletion, thus we
			 * need to reference default_eaction object.
			 */
			if (ipfw_reset_eaction(ch, ch->map[i], eaction_id,
			    no->kidx, instance_id) != 0)
				no->refcnt++;
		}
	}
	/*
	 * Reset eaction opcodes for orphaned dynamic states.
	 * Since parent rules are already deleted, we don't need to
	 * reference named object of default_eaction.
	 */
	ipfw_dyn_reset_eaction(ch, eaction_id, no->kidx, instance_id);
	IPFW_WUNLOCK(ch);
}

/*
 * Initialize external actions framework.
 * Create object with default eaction handler "drop".
 */
int
ipfw_eaction_init(struct ip_fw_chain *ch, int first)
{
	int error;

	error = create_eaction_obj(ch, default_eaction,
	    default_eaction_typename, NULL);
	if (error != 0)
		return (error);
	IPFW_ADD_OBJ_REWRITER(first, eaction_opcodes);
	EACTION_DEBUG("External actions support initialized");
	return (0);
}

void
ipfw_eaction_uninit(struct ip_fw_chain *ch, int last)
{
	struct namedobj_instance *ni;
	struct named_object *no;

	ni = CHAIN_TO_SRV(ch);

	IPFW_UH_WLOCK(ch);
	no = ipfw_objhash_lookup_name_type(ni, 0, IPFW_TLV_EACTION,
	    default_eaction_typename);
	if (no != NULL)
		destroy_eaction_obj(ch, no);
	IPFW_UH_WUNLOCK(ch);
	IPFW_DEL_OBJ_REWRITER(last, eaction_opcodes);
	EACTION_DEBUG("External actions support uninitialized");
}

/*
 * Registers external action handler to the global array.
 * On success it returns eaction id, otherwise - zero.
 */
uint16_t
ipfw_add_eaction(struct ip_fw_chain *ch, ipfw_eaction_t handler,
    const char *name)
{
	uint16_t eaction_id;

	eaction_id = 0;
	if (ipfw_check_object_name_generic(name) == 0) {
		create_eaction_obj(ch, handler, name, &eaction_id);
		EACTION_DEBUG("Registered external action '%s' with id %u",
		    name, eaction_id);
	}
	return (eaction_id);
}

/*
 * Deregisters external action handler with id eaction_id.
 */
int
ipfw_del_eaction(struct ip_fw_chain *ch, uint16_t eaction_id)
{
	struct named_object *no;

	IPFW_UH_WLOCK(ch);
	no = ipfw_objhash_lookup_kidx(CHAIN_TO_SRV(ch), eaction_id);
	if (no == NULL || no->etlv != IPFW_TLV_EACTION) {
		IPFW_UH_WUNLOCK(ch);
		return (EINVAL);
	}
	reset_eaction_rules(ch, eaction_id, 0, (no->refcnt > 1));
	EACTION_DEBUG("External action '%s' with id %u unregistered",
	    no->name, eaction_id);
	destroy_eaction_obj(ch, no);
	IPFW_UH_WUNLOCK(ch);
	return (0);
}

int
ipfw_reset_eaction(struct ip_fw_chain *ch, struct ip_fw *rule,
    uint16_t eaction_id, uint16_t default_id, uint16_t instance_id)
{
	ipfw_insn *cmd, *icmd;

	IPFW_UH_WLOCK_ASSERT(ch);
	IPFW_WLOCK_ASSERT(ch);

	cmd = ACTION_PTR(rule);
	if (cmd->opcode != O_EXTERNAL_ACTION ||
	    cmd->arg1 != eaction_id)
		return (0);

	if (instance_id != 0 && rule->act_ofs < rule->cmd_len - 1) {
		icmd = cmd + 1;
		if (icmd->opcode != O_EXTERNAL_INSTANCE ||
		    icmd->arg1 != instance_id)
			return (0);
		/* FALLTHROUGH */
	}

	cmd->arg1 = default_id; /* Set to default id */
	/*
	 * Since named_object related to this instance will be
	 * also destroyed, truncate the chain of opcodes to
	 * remove the rest of cmd chain just after O_EXTERNAL_ACTION
	 * opcode.
	 */
	if (rule->act_ofs < rule->cmd_len - 1) {
		EACTION_DEBUG("truncate rule %d: len %u -> %u",
		    rule->rulenum, rule->cmd_len, rule->act_ofs + 1);
		rule->cmd_len = rule->act_ofs + 1;
	}
	/*
	 * Return 1 when reset successfully happened.
	 */
	return (1);
}

/*
 * This function should be called before external action instance is
 * destroyed. It will reset eaction_id to default_id for rules, where
 * eaction has instance with id == kidx.
 */
int
ipfw_reset_eaction_instance(struct ip_fw_chain *ch, uint16_t eaction_id,
    uint16_t kidx)
{
	struct named_object *no;

	IPFW_UH_WLOCK_ASSERT(ch);
	no = ipfw_objhash_lookup_kidx(CHAIN_TO_SRV(ch), eaction_id);
	if (no == NULL || no->etlv != IPFW_TLV_EACTION)
		return (EINVAL);

	reset_eaction_rules(ch, eaction_id, kidx, 0);
	return (0);
}

int
ipfw_run_eaction(struct ip_fw_chain *ch, struct ip_fw_args *args,
    ipfw_insn *cmd, int *done)
{

	return (EACTION_OBJ(ch, cmd)->handler(ch, args, cmd, done));
}
