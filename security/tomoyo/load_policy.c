/*
 * security/tomoyo/load_policy.c
 *
 * Policy loader launcher for TOMOYO.
 *
 * Copyright (C) 2005-2010  NTT DATA CORPORATION
 */

#include "common.h"

/* path to policy loader */
static const char *tomoyo_loader = "/sbin/tomoyo-init";

/**
 * tomoyo_policy_loader_exists - Check whether /sbin/tomoyo-init exists.
 *
 * Returns true if /sbin/tomoyo-init exists, false otherwise.
 */
static bool tomoyo_policy_loader_exists(void)
{
	/*
	 * Don't activate MAC if the policy loader doesn't exist.
	 * If the initrd includes /sbin/init but real-root-dev has not
	 * mounted on / yet, activating MAC will block the system since
	 * policies are not loaded yet.
	 * Thus, let do_execve() call this function everytime.
	 */
	struct path path;

	if (kern_path(tomoyo_loader, LOOKUP_FOLLOW, &path)) {
		printk(KERN_INFO "Not activating Mandatory Access Control now "
		       "since %s doesn't exist.\n", tomoyo_loader);
		return false;
	}
	path_put(&path);
	return true;
}

/**
 * tomoyo_load_policy - Run external policy loader to load policy.
 *
 * @filename: The program about to start.
 *
 * This function checks whether @filename is /sbin/init , and if so
 * invoke /sbin/tomoyo-init and wait for the termination of /sbin/tomoyo-init
 * and then continues invocation of /sbin/init.
 * /sbin/tomoyo-init reads policy files in /etc/tomoyo/ directory and
 * writes to /sys/kernel/security/tomoyo/ interfaces.
 *
 * Returns nothing.
 */
void tomoyo_load_policy(const char *filename)
{
	char *argv[2];
	char *envp[3];

	if (tomoyo_policy_loaded)
		return;
	/*
	 * Check filename is /sbin/init or /sbin/tomoyo-start.
	 * /sbin/tomoyo-start is a dummy filename in case where /sbin/init can't
	 * be passed.
	 * You can create /sbin/tomoyo-start by
	 * "ln -s /bin/true /sbin/tomoyo-start".
	 */
	if (strcmp(filename, "/sbin/init") &&
	    strcmp(filename, "/sbin/tomoyo-start"))
		return;
	if (!tomoyo_policy_loader_exists())
		return;

	printk(KERN_INFO "Calling %s to load policy. Please wait.\n",
	       tomoyo_loader);
	argv[0] = (char *) tomoyo_loader;
	argv[1] = NULL;
	envp[0] = "HOME=/";
	envp[1] = "PATH=/sbin:/bin:/usr/sbin:/usr/bin";
	envp[2] = NULL;
	call_usermodehelper(argv[0], argv, envp, 1);
	tomoyo_check_profile();
}
