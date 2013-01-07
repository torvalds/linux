
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
#include <linux/parser.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/statfs.h>
#include <linux/string.h>


#include <linux/ceph/libceph.h>
#include <linux/ceph/debugfs.h>
#include <linux/ceph/decode.h>
#include <linux/ceph/mon_client.h>
#include <linux/ceph/auth.h>
#include "crypto.h"



/*
 * find filename portion of a path (/foo/bar/baz -> baz)
 */
const char *ceph_file_part(const char *s, int len)
{
	const char *e = s + len;

	while (e != s && *(e-1) != '/')
		e--;
	return e;
}
EXPORT_SYMBOL(ceph_file_part);

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
	case CEPH_MSG_MDS_MAP: return "mds_map";
	case CEPH_MSG_CLIENT_SESSION: return "client_session";
	case CEPH_MSG_CLIENT_RECONNECT: return "client_reconnect";
	case CEPH_MSG_CLIENT_REQUEST: return "client_request";
	case CEPH_MSG_CLIENT_REQUEST_FORWARD: return "client_request_forward";
	case CEPH_MSG_CLIENT_REPLY: return "client_reply";
	case CEPH_MSG_CLIENT_CAPS: return "client_caps";
	case CEPH_MSG_CLIENT_CAPRELEASE: return "client_cap_release";
	case CEPH_MSG_CLIENT_SNAP: return "client_snap";
	case CEPH_MSG_CLIENT_LEASE: return "client_lease";
	case CEPH_MSG_OSD_MAP: return "osd_map";
	case CEPH_MSG_OSD_OP: return "osd_op";
	case CEPH_MSG_OSD_OPREPLY: return "osd_opreply";
	case CEPH_MSG_WATCH_NOTIFY: return "watch_notify";
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
	dout("parse_fsid ret %d got fsid %pU", err, fsid);
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
};

static match_table_t opt_tokens = {
	{Opt_osdtimeout, "osdtimeout=%d"},
	{Opt_osdkeepalivetimeout, "osdkeepalive=%d"},
	{Opt_mount_timeout, "mount_timeout=%d"},
	{Opt_osd_idle_ttl, "osd_idle_ttl=%d"},
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

	ukey = request_key(&key_type_ceph, name, NULL);
	if (!ukey || IS_ERR(ukey)) {
		/* request_key errors don't map nicely to mount(2)
		   errors; don't even try, but still printk */
		key_err = PTR_ERR(ukey);
		switch (key_err) {
		case -ENOKEY:
			pr_warning("ceph: Mount failed due to key not found: %s\n", name);
			break;
		case -EKEYEXPIRED:
			pr_warning("ceph: Mount failed due to expired key: %s\n", name);
			break;
		case -EKEYREVOKED:
			pr_warning("ceph: Mount failed due to revoked key: %s\n", name);
			break;
		default:
			pr_warning("ceph: Mount failed due to unknown key error"
			       " %d: %s\n", key_err, name);
		}
		err = -EPERM;
		goto out;
	}

	ckey = ukey->payload.data;
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
	opt->osd_timeout = CEPH_OSD_TIMEOUT_DEFAULT;
	opt->osd_keepalive_timeout = CEPH_OSD_KEEPALIVE_DEFAULT;
	opt->mount_timeout = CEPH_MOUNT_TIMEOUT_DEFAULT; /* seconds */
	opt->osd_idle_ttl = CEPH_OSD_IDLE_TTL_DEFAULT;   /* seconds */

	/* get mon ip(s) */
	/* ip1[:port1][,ip2[:port2]...] */
	err = ceph_parse_ips(dev_name, dev_name_end, opt->mon_addr,
			     CEPH_MAX_MON, &opt->num_mon);
	if (err < 0)
		goto out;

	/* parse mount options */
	while ((c = strsep(&options, ",")) != NULL) {
		int token, intval, ret;
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
			ret = match_int(&argstr[0], &intval);
			if (ret < 0) {
				pr_err("bad mount option arg (not int) "
				       "at '%s'\n", c);
				continue;
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
			opt->name = kstrndup(argstr[0].from,
					      argstr[0].to-argstr[0].from,
					      GFP_KERNEL);
			break;
		case Opt_secret:
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
			opt->osd_timeout = intval;
			break;
		case Opt_osdkeepalivetimeout:
			opt->osd_keepalive_timeout = intval;
			break;
		case Opt_osd_idle_ttl:
			opt->osd_idle_ttl = intval;
			break;
		case Opt_mount_timeout:
			opt->mount_timeout = intval;
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

u64 ceph_client_id(struct ceph_client *client)
{
	return client->monc.auth->global_id;
}
EXPORT_SYMBOL(ceph_client_id);

/*
 * create a fresh client instance
 */
struct ceph_client *ceph_create_client(struct ceph_options *opt, void *private,
				       unsigned supported_features,
				       unsigned required_features)
{
	struct ceph_client *client;
	struct ceph_entity_addr *myaddr = NULL;
	int err = -ENOMEM;

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (client == NULL)
		return ERR_PTR(-ENOMEM);

	client->private = private;
	client->options = opt;

	mutex_init(&client->mount_mutex);
	init_waitqueue_head(&client->auth_wq);
	client->auth_err = 0;

	client->extra_mon_dispatch = NULL;
	client->supported_features = CEPH_FEATURE_SUPPORTED_DEFAULT |
		supported_features;
	client->required_features = CEPH_FEATURE_REQUIRED_DEFAULT |
		required_features;

	/* msgr */
	if (ceph_test_opt(client, MYIP))
		myaddr = &client->options->my_addr;
	ceph_messenger_init(&client->msgr, myaddr,
		client->supported_features,
		client->required_features,
		ceph_test_opt(client, NOCRC));

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

	ceph_debugfs_client_cleanup(client);

	ceph_destroy_options(client->options);

	kfree(client);
	dout("destroy_client %p done\n", client);
}
EXPORT_SYMBOL(ceph_destroy_client);

/*
 * true if we have the mon map (and have thus joined the cluster)
 */
static int have_mon_and_osd_map(struct ceph_client *client)
{
	return client->monc.monmap && client->monc.monmap->epoch &&
	       client->osdc.osdmap && client->osdc.osdmap->epoch;
}

/*
 * mount: join the ceph cluster, and open root directory.
 */
int __ceph_open_session(struct ceph_client *client, unsigned long started)
{
	int err;
	unsigned long timeout = client->options->mount_timeout * HZ;

	/* open session, and wait for mon and osd maps */
	err = ceph_monc_open_session(&client->monc);
	if (err < 0)
		return err;

	while (!have_mon_and_osd_map(client)) {
		err = -EIO;
		if (timeout && time_after_eq(jiffies, started + timeout))
			return err;

		/* wait */
		dout("mount waiting for mon_map\n");
		err = wait_event_interruptible_timeout(client->auth_wq,
			have_mon_and_osd_map(client) || (client->auth_err < 0),
			timeout);
		if (err == -EINTR || err == -ERESTARTSYS)
			return err;
		if (client->auth_err < 0)
			return client->auth_err;
	}

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

	pr_info("loaded (mon/osd proto %d/%d, osdmap %d/%d %d/%d)\n",
		CEPH_MONC_PROTOCOL, CEPH_OSDC_PROTOCOL,
		CEPH_OSDMAP_VERSION, CEPH_OSDMAP_VERSION_EXT,
		CEPH_OSDMAP_INC_VERSION, CEPH_OSDMAP_INC_VERSION_EXT);

	return 0;

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
	ceph_msgr_exit();
	ceph_crypto_shutdown();
	ceph_debugfs_cleanup();
}

module_init(init_ceph_lib);
module_exit(exit_ceph_lib);

MODULE_AUTHOR("Sage Weil <sage@newdream.net>");
MODULE_AUTHOR("Yehuda Sadeh <yehuda@hq.newdream.net>");
MODULE_AUTHOR("Patience Warnick <patience@newdream.net>");
MODULE_DESCRIPTION("Ceph filesystem for Linux");
MODULE_LICENSE("GPL");
