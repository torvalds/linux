
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/vmalloc.h>
#include <linux/kdebug.h>


static ATOMIC_NOTIFIER_HEAD(die_chain);

int notify_die(enum die_val val, const char *str,
	       struct pt_regs *regs, long err, int trap, int sig)
{
	struct die_args args = {
		.regs		= regs,
		.str		= str,
		.err		= err,
		.trapnr		= trap,
		.signr		= sig,

	};

	return atomic_notifier_call_chain(&die_chain, val, &args);
}

int register_die_notifier(struct notifier_block *nb)
{
	vmalloc_sync_all();
	return atomic_notifier_chain_register(&die_chain, nb);
}
EXPORT_SYMBOL_GPL(register_die_notifier);

int unregister_die_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(&die_chain, nb);
}
EXPORT_SYMBOL_GPL(unregister_die_notifier);


