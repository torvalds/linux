// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019-2021 Linaro Ltd.
 *
 * Author:
 * Sumit Garg <sumit.garg@linaro.org>
 */

#include <linux/err.h>
#include <linux/key-type.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/tee_drv.h>
#include <linux/uuid.h>

#include <keys/trusted_tee.h>

#define DRIVER_NAME "trusted-key-tee"

/*
 * Get random data for symmetric key
 *
 * [out]     memref[0]        Random data
 */
#define TA_CMD_GET_RANDOM	0x0

/*
 * Seal trusted key using hardware unique key
 *
 * [in]      memref[0]        Plain key
 * [out]     memref[1]        Sealed key datablob
 */
#define TA_CMD_SEAL		0x1

/*
 * Unseal trusted key using hardware unique key
 *
 * [in]      memref[0]        Sealed key datablob
 * [out]     memref[1]        Plain key
 */
#define TA_CMD_UNSEAL		0x2

/**
 * struct trusted_key_tee_private - TEE Trusted key private data
 * @dev:		TEE based Trusted key device.
 * @ctx:		TEE context handler.
 * @session_id:		Trusted key TA session identifier.
 * @shm_pool:		Memory pool shared with TEE device.
 */
struct trusted_key_tee_private {
	struct device *dev;
	struct tee_context *ctx;
	u32 session_id;
	struct tee_shm *shm_pool;
};

static struct trusted_key_tee_private pvt_data;

/*
 * Have the TEE seal(encrypt) the symmetric key
 */
static int trusted_tee_seal(struct trusted_key_payload *p, char *datablob)
{
	int ret;
	struct tee_ioctl_invoke_arg inv_arg;
	struct tee_param param[4];
	struct tee_shm *reg_shm = NULL;

	memset(&inv_arg, 0, sizeof(inv_arg));
	memset(&param, 0, sizeof(param));

	reg_shm = tee_shm_register_kernel_buf(pvt_data.ctx, p->key,
					      sizeof(p->key) + sizeof(p->blob));
	if (IS_ERR(reg_shm)) {
		dev_err(pvt_data.dev, "shm register failed\n");
		return PTR_ERR(reg_shm);
	}

	inv_arg.func = TA_CMD_SEAL;
	inv_arg.session = pvt_data.session_id;
	inv_arg.num_params = 4;

	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT;
	param[0].u.memref.shm = reg_shm;
	param[0].u.memref.size = p->key_len;
	param[0].u.memref.shm_offs = 0;
	param[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_OUTPUT;
	param[1].u.memref.shm = reg_shm;
	param[1].u.memref.size = sizeof(p->blob);
	param[1].u.memref.shm_offs = sizeof(p->key);

	ret = tee_client_invoke_func(pvt_data.ctx, &inv_arg, param);
	if ((ret < 0) || (inv_arg.ret != 0)) {
		dev_err(pvt_data.dev, "TA_CMD_SEAL invoke err: %x\n",
			inv_arg.ret);
		ret = -EFAULT;
	} else {
		p->blob_len = param[1].u.memref.size;
	}

	tee_shm_free(reg_shm);

	return ret;
}

/*
 * Have the TEE unseal(decrypt) the symmetric key
 */
static int trusted_tee_unseal(struct trusted_key_payload *p, char *datablob)
{
	int ret;
	struct tee_ioctl_invoke_arg inv_arg;
	struct tee_param param[4];
	struct tee_shm *reg_shm = NULL;

	memset(&inv_arg, 0, sizeof(inv_arg));
	memset(&param, 0, sizeof(param));

	reg_shm = tee_shm_register_kernel_buf(pvt_data.ctx, p->key,
					      sizeof(p->key) + sizeof(p->blob));
	if (IS_ERR(reg_shm)) {
		dev_err(pvt_data.dev, "shm register failed\n");
		return PTR_ERR(reg_shm);
	}

	inv_arg.func = TA_CMD_UNSEAL;
	inv_arg.session = pvt_data.session_id;
	inv_arg.num_params = 4;

	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT;
	param[0].u.memref.shm = reg_shm;
	param[0].u.memref.size = p->blob_len;
	param[0].u.memref.shm_offs = sizeof(p->key);
	param[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_OUTPUT;
	param[1].u.memref.shm = reg_shm;
	param[1].u.memref.size = sizeof(p->key);
	param[1].u.memref.shm_offs = 0;

	ret = tee_client_invoke_func(pvt_data.ctx, &inv_arg, param);
	if ((ret < 0) || (inv_arg.ret != 0)) {
		dev_err(pvt_data.dev, "TA_CMD_UNSEAL invoke err: %x\n",
			inv_arg.ret);
		ret = -EFAULT;
	} else {
		p->key_len = param[1].u.memref.size;
	}

	tee_shm_free(reg_shm);

	return ret;
}

/*
 * Have the TEE generate random symmetric key
 */
static int trusted_tee_get_random(unsigned char *key, size_t key_len)
{
	int ret;
	struct tee_ioctl_invoke_arg inv_arg;
	struct tee_param param[4];
	struct tee_shm *reg_shm = NULL;

	memset(&inv_arg, 0, sizeof(inv_arg));
	memset(&param, 0, sizeof(param));

	reg_shm = tee_shm_register_kernel_buf(pvt_data.ctx, key, key_len);
	if (IS_ERR(reg_shm)) {
		dev_err(pvt_data.dev, "key shm register failed\n");
		return PTR_ERR(reg_shm);
	}

	inv_arg.func = TA_CMD_GET_RANDOM;
	inv_arg.session = pvt_data.session_id;
	inv_arg.num_params = 4;

	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_OUTPUT;
	param[0].u.memref.shm = reg_shm;
	param[0].u.memref.size = key_len;
	param[0].u.memref.shm_offs = 0;

	ret = tee_client_invoke_func(pvt_data.ctx, &inv_arg, param);
	if ((ret < 0) || (inv_arg.ret != 0)) {
		dev_err(pvt_data.dev, "TA_CMD_GET_RANDOM invoke err: %x\n",
			inv_arg.ret);
		ret = -EFAULT;
	} else {
		ret = param[0].u.memref.size;
	}

	tee_shm_free(reg_shm);

	return ret;
}

static int optee_ctx_match(struct tee_ioctl_version_data *ver, const void *data)
{
	if (ver->impl_id == TEE_IMPL_ID_OPTEE &&
	    ver->gen_caps & TEE_GEN_CAP_REG_MEM)
		return 1;
	else
		return 0;
}

static int trusted_key_probe(struct device *dev)
{
	struct tee_client_device *rng_device = to_tee_client_device(dev);
	int ret;
	struct tee_ioctl_open_session_arg sess_arg;

	memset(&sess_arg, 0, sizeof(sess_arg));

	pvt_data.ctx = tee_client_open_context(NULL, optee_ctx_match, NULL,
					       NULL);
	if (IS_ERR(pvt_data.ctx))
		return -ENODEV;

	memcpy(sess_arg.uuid, rng_device->id.uuid.b, TEE_IOCTL_UUID_LEN);
	sess_arg.clnt_login = TEE_IOCTL_LOGIN_REE_KERNEL;
	sess_arg.num_params = 0;

	ret = tee_client_open_session(pvt_data.ctx, &sess_arg, NULL);
	if ((ret < 0) || (sess_arg.ret != 0)) {
		dev_err(dev, "tee_client_open_session failed, err: %x\n",
			sess_arg.ret);
		ret = -EINVAL;
		goto out_ctx;
	}
	pvt_data.session_id = sess_arg.session;

	ret = register_key_type(&key_type_trusted);
	if (ret < 0)
		goto out_sess;

	pvt_data.dev = dev;

	return 0;

out_sess:
	tee_client_close_session(pvt_data.ctx, pvt_data.session_id);
out_ctx:
	tee_client_close_context(pvt_data.ctx);

	return ret;
}

static int trusted_key_remove(struct device *dev)
{
	unregister_key_type(&key_type_trusted);
	tee_client_close_session(pvt_data.ctx, pvt_data.session_id);
	tee_client_close_context(pvt_data.ctx);

	return 0;
}

static const struct tee_client_device_id trusted_key_id_table[] = {
	{UUID_INIT(0xf04a0fe7, 0x1f5d, 0x4b9b,
		   0xab, 0xf7, 0x61, 0x9b, 0x85, 0xb4, 0xce, 0x8c)},
	{}
};
MODULE_DEVICE_TABLE(tee, trusted_key_id_table);

static struct tee_client_driver trusted_key_driver = {
	.id_table	= trusted_key_id_table,
	.driver		= {
		.name		= DRIVER_NAME,
		.bus		= &tee_bus_type,
		.probe		= trusted_key_probe,
		.remove		= trusted_key_remove,
	},
};

static int trusted_tee_init(void)
{
	return driver_register(&trusted_key_driver.driver);
}

static void trusted_tee_exit(void)
{
	driver_unregister(&trusted_key_driver.driver);
}

struct trusted_key_ops trusted_key_tee_ops = {
	.migratable = 0, /* non-migratable */
	.init = trusted_tee_init,
	.seal = trusted_tee_seal,
	.unseal = trusted_tee_unseal,
	.get_random = trusted_tee_get_random,
	.exit = trusted_tee_exit,
};
