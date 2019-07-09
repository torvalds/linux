// SPDX-License-Identifier: GPL-2.0-only

#include <linux/ceph/ceph_debug.h>
#include <linux/backing-dev.h>
#include <linux/ctype.h>
#include <linux/fs.h>
#include <linux/inet.h>
#include <linux/in6.h>
#include <linux/key.h>
#include <keys/ceph-type.h>
#include <linux/module.h>
#include <linux/mount.h>
#include <linux/nsproxy.h>
#include <linux/parser.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/statfs.h>
#include <linux/string.h>
#include <linux/vmalloc.h>


#include <linux/ceph/ceph_features.h>
#include <linux/ceph/libceph.h>
#include <linux/ceph/debugfs.h>
#include <linux/ceph/decode.h>
#include <linux/ceph/mon_client.h>
#include <linux/ceph/auth.h>
#include "crypto.h"


/*
 * Module compatibility interface.  For now it doesn't do anything,
 * but its existence signals a certain level of functionality.
 *
 * The data buffer is used to pass information both to and from
 * libceph.  The return value indicates whether libceph determines
 * it is compatible with the caller (from another kernel module),
 * given the provided data.
 *
 * The data pointer can be null.
 */
bool libceph_compatible(void *data)
{
	return true;
}
EXPORT_SYMBOL(libceph_compatible);

static int param_get_supported_features(char *buffer,
					const struct kernel_param *kp)
{
	return sprintf(buffer, "0x%llx", CEPH_FEATURES_SUPPORTED_DEFAULT);
}
static const struct kernel_param_ops param_ops_supported_features = {
	.get = param_get_supported_features,
};
module_param_cb(supported_features, &param_ops_supported_features, NULL,
		0444);

const char *ceph_msg_type_name(int type)
{
	switch (type) {
	case CEPH_MSG_SHUTDOWN: return "shutdown";
	case CEPH_MSG_PING: return "ping";
	case CEPH_MSG_AUTH: return "auth";
	case CEPH_MSG_AUTH_REPLY: return "auth_reply";
	case CEPH_MSG_MON_MAP: return "mon_map";
	case CEPH_MSG_MON_GET_MAP: return "mon_get_map";
	case CEPH_MSG_MON_SUBSCRIBE: return "mon_subscribe";
	case CEPH_MSG_MON_SUBSCRIBE_ACK: return "mon_subscribe_ack";
	case CEPH_MSG_STATFS: return "statfs";
	case CEPH_MSG_STATFS_REPLY: return "statfs_reply";
	case CEPH_MSG_MON_GET_VERSION: return "mon_get_version";
	case CEPH_MSG_MON_GET_VERSION_REPLY: return "mon_get_version_reply";
	case CEPH_MSG_MDS_MAP: return "mds_map";
	case CEPH_MSG_FS_MAP_USER: return "fs_map_user";
	case CEPH_MSG_CLIENT_SESSION: return "client_session";
	case CEPH_MSG_CLIENT_RECONNECT: return "client_reconnect";
	case CEPH_MSG_CLIENT_REQUEST: return "client_request";
	case CEPH_MSG_CLIENT_REQUEST_FORWARD: return "client_request_forward";
	case CEPH_MSG_CLIENT_REPLY: return "client_reply";
	case CEPH_MSG_CLIENT_CAPS: return "client_caps";
	case CEPH_MSG_CLIENT_CAPRELEASE: return "client_cap_release";
	case CEPH_MSG_CLIENT_QUOTA: return "client_quota";
	case CEPH_MSG_CLIENT_SNAP: return "client_snap";
	case CEPH_MSG_CLIENT_LEASE: return "client_lease";
	case CEPH_MSG_POOLOP_REPLY: return "poolop_reply";
	case CEPH_MSG_POOLOP: return "poolop";
	case CEPH_MSG_MON_COMMAND: return "mon_command";
	case CEPH_MSG_MON_COMMAND_ACK: return "mon_command_ack";
	case CEPH_MSG_OSD_MAP: return "osd_map";
	case CEPH_MSG_OSD_OP: return "osd_op";
	case CEPH_MSG_OSD_OPREPLY: return "osd_opreply";
	case CEPH_MSG_WATCH_NOTIFY: return "watch_notify";
	case CEPH_MSG_OSD_BACKOFF: return "osd_backoff";
	default: return "unknown";
	}
}
EXPORT_SYMBOL(ceph_msg_type_name);

/*
 * Initially learn our fsid, or verify an fsid matches.
 */
int ceph_check_fsid(struct ceph_client *client, struct ceph_fsid *fsid)
{
	if (client->have_fsid) {
		if (ceph_fsid_compare(&client->fsid, fsid)) {
			pr_err("bad fsid, had %pU got %pU",
			       &client->fsid, fsid);
			return -1;
		}
	} else {
		memcpy(&client->fsid, fsid, sizeof(*fsid));
	}
	return 0;
}
EXPORT_SYMBOL(ceph_check_fsid);

static int strcmp_null(const char *s1, const char *s2)
{
	if (!s1 && !s2)
		return 0;
	if (s1 && !s2)
		return -1;
	if (!s1 && s2)
		return 1;
	return strcmp(s1, s2);
}

int ceph_compare_options(struct ceph_options *new_opt,
			 struct ceph_client *client)
{
	struct ceph_options *opt1 = new_opt;
	struct ceph_options *opt2 = client->options;
	int ofs = offsetof(struct ceph_options, mon_addr);
	int i;
	int ret;

	/*
	 * Don't bother comparing options if network namespaces don't
	 * match.
	 */
	if (!net_eq(current->nsproxy->net_ns, read_pnet(&client->msgr.net)))
		return -1;

	ret = memcmp(opt1, opt2, ofs);
	if (ret)
		return ret;

	ret = strcmp_null(opt1->name, opt2->name);
	if (ret)
		return ret;

	if (opt1->key && !opt2->key)
		return -1;
	if (!opt1->key && opt2->key)
		return 1;
	if (opt1->key && opt2->key) {
		if (opt1->key->type != opt2->key->type)
			return -1;
		if (opt1->key->created.tv_sec != opt2->key->created.tv_sec)
			return -1;
		if (opt1->key->created.tv_nsec != opt2->key->created.tv_nsec)
			return -1;
		if (opt1->key->len != opt2->key->len)
			return -1;
		if (opt1->key->key && !opt2->key->key)
			return -1;
		if (!opt1->key->key && opt2->key->key)
			return 1;
		if (opt1->key->key && opt2->key->key) {
			ret = memcmp(opt1->key->key, opt2->key->key, opt1->key->len);
			if (ret)
				return ret;
		}
	}

	/* any matching mon ip implies a match */
	for (i = 0; i < opt1->num_mon; i++) {
		if (ceph_monmap_contains(client->monc.monmap,
				 &opt1->mon_addr[i]))
			return 0;
	}
	return -1;
}
EXPORT_SYMBOL(ceph_compare_options);

void *ceph_kvmalloc(size_t size, gfp_t flags)
{
	if (size <= (PAGE_SIZE << PAGE_ALLOC_COSTLY_ORDER)) {
		void *ptr = kmalloc(size, flags | __GFP_NOWARN);
		if (ptr)
			return ptr;
	}

	return __vmalloc(size, flags, PAGE_KERNEL);
}


static int parse_fsid(const char *str, struct ceph_fsid *fsid)
{
	int i = 0;
	char tmp[3];
	int err = -EINVAL;
	int d;

	dout("parse_fsid '%s'\n", str);
	tmp[2] = 0;
	while (*str && i < 16) {
		if (ispunct(*str)) {
			str++;
			continue;
		}
		if (!isxdigit(str[0]) || !isxdigit(str[1]))
			break;
		tmp[0] = str[0];
		tmp[1] = str[1];
		if (sscanf(tmp, "%x", &d) < 1)
			break;
		fsid->fsid[i] = d & 0xff;
		i++;
		str += 2;
	}

	if (i == 16)
		err = 0;
	dout("parse_fsid ret %d got fsid %pU\n", err, fsid);
	return err;
}

/*
 * ceph options
 */
enum {
	Opt_osdtimeout,
	Opt_osdkeepalivetimeout,
	Opt_mount_timeout,
	Opt_osd_idle_ttl,
	Opt_osd_request_timeout,
	Opt_last_int,
	/* int args above */
	Opt_fsid,
	Opt_name,
	Opt_secret,
	Opt_key,
	Opt_ip,
	Opt_last_string,
	/* string args above */
	Opt_share,
	Opt_noshare,
	Opt_crc,
	Opt_nocrc,
	Opt_cephx_require_signatures,
	Opt_nocephx_require_signatures,
	Opt_cephx_sign_messages,
	Opt_nocephx_sign_messages,
	Opt_tcp_nodelay,
	Opt_notcp_nodelay,
	Opt_abort_on_full,
};

static match_table_t opt_tokens = {
	{Opt_osdtimeout, "osdtimeout=%d"},
	{Opt_osdkeepalivetimeout, "osdkeepalive=%d"},
	{Opt_mount_timeout, "mount_timeout=%d"},
	{Opt_osd_idle_ttl, "osd_idle_ttl=%d"},
	{Opt_osd_request_timeout, "osd_request_timeout=%d"},
	/* int args above */
	{Opt_fsid, "fsid=%s"},
	{Opt_name, "name=%s"},
	{Opt_secret, "secret=%s"},
	{Opt_key, "key=%s"},
	{Opt_ip, "ip=%s"},
	/* string args above */
	{Opt_share, "share"},
	{Opt_noshare, "noshare"},
	{Opt_crc, "crc"},
	{Opt_nocrc, "nocrc"},
	{Opt_cephx_require_signatures, "cephx_require_signatures"},
	{Opt_nocephx_require_signatures, "nocephx_require_signatures"},
	{Opt_cephx_sign_messages, "cephx_sign_messages"},
	{Opt_nocephx_sign_messages, "nocephx_sign_messages"},
	{Opt_tcp_nodelay, "tcp_nodelay"},
	{Opt_notcp_nodelay, "notcp_nodelay"},
	{Opt_abort_on_full, "abort_on_full"},
	{-1, NULL}
};

void ceph_destroy_options(struct ceph_options *opt)
{
	dout("destroy_options %p\n", opt);
	kfree(opt->name);
	if (opt->key) {
		ceph_crypto_key_destroy(opt->key);
		kfree(opt->key);
	}
	kfree(opt->mon_addr);
	kfree(opt);
}
EXPORT_SYMBOL(ceph_destroy_options);

/* get secret from key store */
static int get_secret(struct ceph_crypto_key *dst, const char *name) {
	struct key *ukey;
	int key_err;
	int err = 0;
	struct ceph_crypto_key *ckey;

	ukey = request_key(&key_type_ceph, name, NULL, NULL);
	if (IS_ERR(ukey)) {
		/* request_key errors don't map nicely to mount(2)
		   errors; don't even try, but still printk */
		key_err = PTR_ERR(ukey);
		switch (key_err) {
		case -ENOKEY:
			pr_warn("ceph: Mount failed due to key not found: %s\n",
				name);
			break;
		case -EKEYEXPIRED:
			pr_warn("ceph: Mount failed due to expired key: %s\n",
				name);
			break;
		case -EKEYREVOKED:
			pr_warn("ceph: Mount failed due to revoked key: %s\n",
				name);
			break;
		default:
			pr_warn("ceph: Mount failed due to unknown key error %d: %s\n",
				key_err, name);
		}
		err = -EPERM;
		goto out;
	}

	ckey = ukey->payload.data[0];
	err = ceph_crypto_key_clone(dst, ckey);
	if (err)
		goto out_key;
	/* pass through, err is 0 */

out_key:
	key_put(ukey);
out:
	return err;
}

struct ceph_options *
ceph_parse_options(char *options, const char *dev_name,
			const char *dev_name_end,
			int (*parse_extra_token)(char *c, void *private),
			void *private)
{
	struct ceph_options *opt;
	const char *c;
	int err = -ENOMEM;
	substring_t argstr[MAX_OPT_ARGS];

	opt = kzalloc(sizeof(*opt), GFP_KERNEL);
	if (!opt)
		return ERR_PTR(-ENOMEM);
	opt->mon_addr = kcalloc(CEPH_MAX_MON, sizeof(*opt->mon_addr),
				GFP_KERNEL);
	if (!opt->mon_addr)
		goto out;

	dout("parse_options %p options '%s' dev_name '%s'\n", opt, options,
	     dev_name);

	/* start with defaults */
	opt->flags = CEPH_OPT_DEFAULT;
	opt->osd_keepalive_timeout = CEPH_OSD_KEEPALIVE_DEFAULT;
	opt->mount_timeout = CEPH_MOUNT_TIMEOUT_DEFAULT;
	opt->osd_idle_ttl = CEPH_OSD_IDLE_TTL_DEFAULT;
	opt->osd_request_timeout = CEPH_OSD_REQUEST_TIMEOUT_DEFAULT;

	/* get mon ip(s) */
	/* ip1[:port1][,ip2[:port2]...] */
	err = ceph_parse_ips(dev_name, dev_name_end, opt->mon_addr,
			     CEPH_MAX_MON, &opt->num_mon);
	if (err < 0)
		goto out;

	/* parse mount options */
	while ((c = strsep(&options, ",")) != NULL) {
		int token, intval;
		if (!*c)
			continue;
		err = -EINVAL;
		token = match_token((char *)c, opt_tokens, argstr);
		if (token < 0 && parse_extra_token) {
			/* extra? */
			err = parse_extra_token((char *)c, private);
			if (err < 0) {
				pr_err("bad option at '%s'\n", c);
				goto out;
			}
			continue;
		}
		if (token < Opt_last_int) {
			err = match_int(&argstr[0], &intval);
			if (err < 0) {
				pr_err("bad option arg (not int) at '%s'\n", c);
				goto out;
			}
			dout("got int token %d val %d\n", token, intval);
		} else if (token > Opt_last_int && token < Opt_last_string) {
			dout("got string token %d val %s\n", token,
			     argstr[0].from);
		} else {
			dout("got token %d\n", token);
		}
		switch (token) {
		case Opt_ip:
			err = ceph_parse_ips(argstr[0].from,
					     argstr[0].to,
					     &opt->my_addr,
					     1, NULL);
			if (err < 0)
				goto out;
			opt->flags |= CEPH_OPT_MYIP;
			break;

		case Opt_fsid:
			err = parse_fsid(argstr[0].from, &opt->fsid);
			if (err == 0)
				opt->flags |= CEPH_OPT_FSID;
			break;
		case Opt_name:
			kfree(opt->name);
			opt->name = kstrndup(argstr[0].from,
					      argstr[0].to-argstr[0].from,
					      GFP_KERNEL);
			if (!opt->name) {
				err = -ENOMEM;
				goto out;
			}
			break;
		case Opt_secret:
			ceph_crypto_key_destroy(opt->key);
			kfree(opt->key);

		        opt->key = kzalloc(sizeof(*opt->key), GFP_KERNEL);
			if (!opt->key) {
				err = -ENOMEM;
				goto out;
			}
			err = ceph_crypto_key_unarmor(opt->key, argstr[0].from);
			if (err < 0)
				goto out;
			break;
		case Opt_key:
			ceph_crypto_key_destroy(opt->key);
			kfree(opt->key);

		        opt->key = kzalloc(sizeof(*opt->key), GFP_KERNEL);
			if (!opt->key) {
				err = -ENOMEM;
				goto out;
			}
			err = get_secret(opt->key, argstr[0].from);
			if (err < 0)
				goto out;
			break;

			/* misc */
		case Opt_osdtimeout:
			pr_warn("ignoring deprecated osdtimeout option\n");
			break;
		case Opt_osdkeepalivetimeout:
			/* 0 isn't well defined right now, reject it */
			if (intval < 1 || intval > INT_MAX / 1000) {
				pr_err("osdkeepalive out of range\n");
				err = -EINVAL;
				goto out;
			}
			opt->osd_keepalive_timeout =
					msecs_to_jiffies(intval * 1000);
			break;
		case Opt_osd_idle_ttl:
			/* 0 isn't well defined right now, reject it */
			if (intval < 1 || intval > INT_MAX / 1000) {
				pr_err("osd_idle_ttl out of range\n");
				err = -EINVAL;
				goto out;
			}
			opt->osd_idle_ttl = msecs_to_jiffies(intval * 1000);
			break;
		case Opt_mount_timeout:
			/* 0 is "wait forever" (i.e. infinite timeout) */
			if (intval < 0 || intval > INT_MAX / 1000) {
				pr_err("mount_timeout out of range\n");
				err = -EINVAL;
				goto out;
			}
			opt->mount_timeout = msecs_to_jiffies(intval * 1000);
			break;
		case Opt_osd_request_timeout:
			/* 0 is "wait forever" (i.e. infinite timeout) */
			if (intval < 0 || intval > INT_MAX / 1000) {
				pr_err("osd_request_timeout out of range\n");
				err = -EINVAL;
				goto out;
			}
			opt->osd_request_timeout = msecs_to_jiffies(intval * 1000);
			break;

		case Opt_share:
			opt->flags &= ~CEPH_OPT_NOSHARE;
			break;
		case Opt_noshare:
			opt->flags |= CEPH_OPT_NOSHARE;
			break;

		case Opt_crc:
			opt->flags &= ~CEPH_OPT_NOCRC;
			break;
		case Opt_nocrc:
			opt->flags |= CEPH_OPT_NOCRC;
			break;

		case Opt_cephx_require_signatures:
			opt->flags &= ~CEPH_OPT_NOMSGAUTH;
			break;
		case Opt_nocephx_require_signatures:
			opt->flags |= CEPH_OPT_NOMSGAUTH;
			break;
		case Opt_cephx_sign_messages:
			opt->flags &= ~CEPH_OPT_NOMSGSIGN;
			break;
		case Opt_nocephx_sign_messages:
			opt->flags |= CEPH_OPT_NOMSGSIGN;
			break;

		case Opt_tcp_nodelay:
			opt->flags |= CEPH_OPT_TCP_NODELAY;
			break;
		case Opt_notcp_nodelay:
			opt->flags &= ~CEPH_OPT_TCP_NODELAY;
			break;

		case Opt_abort_on_full:
			opt->flags |= CEPH_OPT_ABORT_ON_FULL;
			break;

		default:
			BUG_ON(token);
		}
	}

	/* success */
	return opt;

out:
	ceph_destroy_options(opt);
	return ERR_PTR(err);
}
EXPORT_SYMBOL(ceph_parse_options);

int ceph_print_client_options(struct seq_file *m, struct ceph_client *client,
			      bool show_all)
{
	struct ceph_options *opt = client->options;
	size_t pos = m->count;

	if (opt->name) {
		seq_puts(m, "name=");
		seq_escape(m, opt->name, ", \t\n\\");
		seq_putc(m, ',');
	}
	if (opt->key)
		seq_puts(m, "secret=<hidden>,");

	if (opt->flags & CEPH_OPT_FSID)
		seq_printf(m, "fsid=%pU,", &opt->fsid);
	if (opt->flags & CEPH_OPT_NOSHARE)
		seq_puts(m, "noshare,");
	if (opt->flags & CEPH_OPT_NOCRC)
		seq_puts(m, "nocrc,");
	if (opt->flags & CEPH_OPT_NOMSGAUTH)
		seq_puts(m, "nocephx_require_signatures,");
	if (opt->flags & CEPH_OPT_NOMSGSIGN)
		seq_puts(m, "nocephx_sign_messages,");
	if ((opt->flags & CEPH_OPT_TCP_NODELAY) == 0)
		seq_puts(m, "notcp_nodelay,");
	if (show_all && (opt->flags & CEPH_OPT_ABORT_ON_FULL))
		seq_puts(m, "abort_on_full,");

	if (opt->mount_timeout != CEPH_MOUNT_TIMEOUT_DEFAULT)
		seq_printf(m, "mount_timeout=%d,",
			   jiffies_to_msecs(opt->mount_timeout) / 1000);
	if (opt->osd_idle_ttl != CEPH_OSD_IDLE_TTL_DEFAULT)
		seq_printf(m, "osd_idle_ttl=%d,",
			   jiffies_to_msecs(opt->osd_idle_ttl) / 1000);
	if (opt->osd_keepalive_timeout != CEPH_OSD_KEEPALIVE_DEFAULT)
		seq_printf(m, "osdkeepalivetimeout=%d,",
		    jiffies_to_msecs(opt->osd_keepalive_timeout) / 1000);
	if (opt->osd_request_timeout != CEPH_OSD_REQUEST_TIMEOUT_DEFAULT)
		seq_printf(m, "osd_request_timeout=%d,",
			   jiffies_to_msecs(opt->osd_request_timeout) / 1000);

	/* drop redundant comma */
	if (m->count != pos)
		m->count--;

	return 0;
}
EXPORT_SYMBOL(ceph_print_client_options);

struct ceph_entity_addr *ceph_client_addr(struct ceph_client *client)
{
	return &client->msgr.inst.addr;
}
EXPORT_SYMBOL(ceph_client_addr);

u64 ceph_client_gid(struct ceph_client *client)
{
	return client->monc.auth->global_id;
}
EXPORT_SYMBOL(ceph_client_gid);

/*
 * create a fresh client instance
 */
struct ceph_client *ceph_create_client(struct ceph_options *opt, void *private)
{
	struct ceph_client *client;
	struct ceph_entity_addr *myaddr = NULL;
	int err;

	err = wait_for_random_bytes();
	if (err < 0)
		return ERR_PTR(err);

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (client == NULL)
		return ERR_PTR(-ENOMEM);

	client->private = private;
	client->options = opt;

	mutex_init(&client->mount_mutex);
	init_waitqueue_head(&client->auth_wq);
	client->auth_err = 0;

	client->extra_mon_dispatch = NULL;
	client->supported_features = CEPH_FEATURES_SUPPORTED_DEFAULT;
	client->required_features = CEPH_FEATURES_REQUIRED_DEFAULT;

	if (!ceph_test_opt(client, NOMSGAUTH))
		client->required_features |= CEPH_FEATURE_MSG_AUTH;

	/* msgr */
	if (ceph_test_opt(client, MYIP))
		myaddr = &client->options->my_addr;

	ceph_messenger_init(&client->msgr, myaddr);

	/* subsystems */
	err = ceph_monc_init(&client->monc, client);
	if (err < 0)
		goto fail;
	err = ceph_osdc_init(&client->osdc, client);
	if (err < 0)
		goto fail_monc;

	return client;

fail_monc:
	ceph_monc_stop(&client->monc);
fail:
	ceph_messenger_fini(&client->msgr);
	kfree(client);
	return ERR_PTR(err);
}
EXPORT_SYMBOL(ceph_create_client);

void ceph_destroy_client(struct ceph_client *client)
{
	dout("destroy_client %p\n", client);

	atomic_set(&client->msgr.stopping, 1);

	/* unmount */
	ceph_osdc_stop(&client->osdc);
	ceph_monc_stop(&client->monc);
	ceph_messenger_fini(&client->msgr);

	ceph_debugfs_client_cleanup(client);

	ceph_destroy_options(client->options);

	kfree(client);
	dout("destroy_client %p done\n", client);
}
EXPORT_SYMBOL(ceph_destroy_client);

/*
 * true if we have the mon map (and have thus joined the cluster)
 */
static bool have_mon_and_osd_map(struct ceph_client *client)
{
	return client->monc.monmap && client->monc.monmap->epoch &&
	       client->osdc.osdmap && client->osdc.osdmap->epoch;
}

/*
 * mount: join the ceph cluster, and open root directory.
 */
int __ceph_open_session(struct ceph_client *client, unsigned long started)
{
	unsigned long timeout = client->options->mount_timeout;
	long err;

	/* open session, and wait for mon and osd maps */
	err = ceph_monc_open_session(&client->monc);
	if (err < 0)
		return err;

	while (!have_mon_and_osd_map(client)) {
		if (timeout && time_after_eq(jiffies, started + timeout))
			return -ETIMEDOUT;

		/* wait */
		dout("mount waiting for mon_map\n");
		err = wait_event_interruptible_timeout(client->auth_wq,
			have_mon_and_osd_map(client) || (client->auth_err < 0),
			ceph_timeout_jiffies(timeout));
		if (err < 0)
			return err;
		if (client->auth_err < 0)
			return client->auth_err;
	}

	pr_info("client%llu fsid %pU\n", ceph_client_gid(client),
		&client->fsid);
	ceph_debugfs_client_init(client);

	return 0;
}
EXPORT_SYMBOL(__ceph_open_session);

int ceph_open_session(struct ceph_client *client)
{
	int ret;
	unsigned long started = jiffies;  /* note the start time */

	dout("open_session start\n");
	mutex_lock(&client->mount_mutex);

	ret = __ceph_open_session(client, started);

	mutex_unlock(&client->mount_mutex);
	return ret;
}
EXPORT_SYMBOL(ceph_open_session);

int ceph_wait_for_latest_osdmap(struct ceph_client *client,
				unsigned long timeout)
{
	u64 newest_epoch;
	int ret;

	ret = ceph_monc_get_version(&client->monc, "osdmap", &newest_epoch);
	if (ret)
		return ret;

	if (client->osdc.osdmap->epoch >= newest_epoch)
		return 0;

	ceph_osdc_maybe_request_map(&client->osdc);
	return ceph_monc_wait_osdmap(&client->monc, newest_epoch, timeout);
}
EXPORT_SYMBOL(ceph_wait_for_latest_osdmap);

static int __init init_ceph_lib(void)
{
	int ret = 0;

	ret = ceph_debugfs_init();
	if (ret < 0)
		goto out;

	ret = ceph_crypto_init();
	if (ret < 0)
		goto out_debugfs;

	ret = ceph_msgr_init();
	if (ret < 0)
		goto out_crypto;

	ret = ceph_osdc_setup();
	if (ret < 0)
		goto out_msgr;

	pr_info("loaded (mon/osd proto %d/%d)\n",
		CEPH_MONC_PROTOCOL, CEPH_OSDC_PROTOCOL);

	return 0;

out_msgr:
	ceph_msgr_exit();
out_crypto:
	ceph_crypto_shutdown();
out_debugfs:
	ceph_debugfs_cleanup();
out:
	return ret;
}

static void __exit exit_ceph_lib(void)
{
	dout("exit_ceph_lib\n");
	WARN_ON(!ceph_strings_empty());

	ceph_osdc_cleanup();
	ceph_msgr_exit();
	ceph_crypto_shutdown();
	ceph_debugfs_cleanup();
}

module_init(init_ceph_lib);
module_exit(exit_ceph_lib);

MODULE_AUTHOR("Sage Weil <sage@newdream.net>");
MODULE_AUTHOR("Yehuda Sadeh <yehuda@hq.newdream.net>");
MODULE_AUTHOR("Patience Warnick <patience@newdream.net>");
MODULE_DESCRIPTION("Ceph core library");
MODULE_LICENSE("GPL");
