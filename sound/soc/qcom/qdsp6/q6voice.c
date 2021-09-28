// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2012-2017, The Linux Foundation. All rights reserved.
// Copyright (c) 2020, Stephan Gerhold

#include <linux/device.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include "q6afe.h"
#include "q6cvp.h"
#include "q6cvs.h"
#include "q6mvm.h"
#include "q6voice-common.h"

struct q6voice_path_runtime {
	struct q6voice_session *sessions[Q6VOICE_SERVICE_COUNT];
	unsigned int started;
};

struct q6voice_path {
	struct q6voice *v;

	enum q6voice_path_type type;
	int tx_port, rx_port;
	/* Serialize access to voice path session */
	struct mutex lock;
	struct q6voice_path_runtime *runtime;
};

struct q6voice {
	struct device *dev;
	struct q6voice_path paths[Q6VOICE_PATH_COUNT];
};

static int q6voice_path_start(struct q6voice_path *p)
{
	struct device *dev = p->v->dev;
	struct q6voice_session *mvm, *cvp;
	int ret;

	dev_dbg(dev, "start path %d\n", p->type);

	mvm = p->runtime->sessions[Q6VOICE_SERVICE_MVM];
	if (!mvm) {
		mvm = q6mvm_session_create(p->type);
		if (IS_ERR(mvm))
			return PTR_ERR(mvm);
		p->runtime->sessions[Q6VOICE_SERVICE_MVM] = mvm;
	}

	cvp = p->runtime->sessions[Q6VOICE_SERVICE_CVP];
	if (!cvp) {
		cvp = q6cvp_session_create(p->type,
					   q6afe_get_port_id(p->tx_port),
					   q6afe_get_port_id(p->rx_port));
		if (IS_ERR(cvp))
			return PTR_ERR(cvp);
		p->runtime->sessions[Q6VOICE_SERVICE_CVP] = cvp;
	}

	ret = q6cvp_enable(cvp, true);
	if (ret) {
		dev_err(dev, "failed to enable cvp: %d\n", ret);
		goto cvp_err;
	}

	ret = q6mvm_attach(mvm, cvp, true);
	if (ret) {
		dev_err(dev, "failed to attach cvp to mvm: %d\n", ret);
		goto attach_err;
	}

	ret = q6mvm_start(mvm, true);
	if (ret) {
		dev_err(dev, "failed to start voice: %d\n", ret);
		goto start_err;
	}

	return ret;

start_err:
	q6mvm_start(mvm, false);
attach_err:
	q6mvm_attach(mvm, cvp, false);
cvp_err:
	q6cvp_enable(cvp, false);
	return ret;
}

int q6voice_start(struct q6voice *v, enum q6voice_path_type path, bool capture)
{
	struct q6voice_path *p = &v->paths[path];
	int ret = 0;

	mutex_lock(&p->lock);
	if (!p->runtime) {
		p->runtime = kzalloc(sizeof(*p), GFP_KERNEL);
		if (!p->runtime) {
			ret = -ENOMEM;
			goto out;
		}
	}

	if (p->runtime->started & BIT(capture)) {
		ret = -EALREADY;
		goto out;
	}

	p->runtime->started |= BIT(capture);

	/* FIXME: For now we only start if both RX/TX are active */
	if (p->runtime->started != 3)
		goto out;

	ret = q6voice_path_start(p);
	if (ret) {
		p->runtime->started &= ~BIT(capture);
		dev_err(v->dev, "failed to start path %d: %d\n", path, ret);
		goto out;
	}

out:
	mutex_unlock(&p->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(q6voice_start);

static void q6voice_path_stop(struct q6voice_path *p)
{
	struct device *dev = p->v->dev;
	struct q6voice_session *mvm = p->runtime->sessions[Q6VOICE_SERVICE_MVM];
	struct q6voice_session *cvp = p->runtime->sessions[Q6VOICE_SERVICE_CVP];
	int ret;

	dev_dbg(dev, "stop path %d\n", p->type);

	ret = q6mvm_start(mvm, false);
	if (ret)
		dev_err(dev, "failed to stop voice: %d\n", ret);

	ret = q6mvm_attach(mvm, cvp, false);
	if (ret)
		dev_err(dev, "failed to detach cvp from mvm: %d\n", ret);

	ret = q6cvp_enable(cvp, false);
	if (ret)
		dev_err(dev, "failed to disable cvp: %d\n", ret);
}

static void q6voice_path_destroy(struct q6voice_path *p)
{
	struct q6voice_path_runtime *runtime = p->runtime;
	enum q6voice_service_type svc;

	for (svc = 0; svc < Q6VOICE_SERVICE_COUNT; ++svc) {
		if (runtime->sessions[svc])
			q6voice_session_release(runtime->sessions[svc]);
	}

	p->runtime = NULL;
	kfree(runtime);
}

int q6voice_stop(struct q6voice *v, enum q6voice_path_type path, bool capture)
{
	struct q6voice_path *p = &v->paths[path];
	int ret = 0;

	mutex_lock(&p->lock);
	if (!p->runtime || !(p->runtime->started & BIT(capture)))
		goto out;

	if (p->runtime->started == 3)
		q6voice_path_stop(p);

	p->runtime->started &= ~BIT(capture);

	if (p->runtime->started == 0)
		q6voice_path_destroy(p);

out:
	mutex_unlock(&p->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(q6voice_stop);

static void q6voice_free(void *data)
{
	struct q6voice *v = data;
	enum q6voice_path_type path;

	for (path = 0; path < Q6VOICE_PATH_COUNT; ++path) {
		struct q6voice_path *p = &v->paths[path];

		mutex_lock(&p->lock);
		if (p->runtime) {
			dev_warn(v->dev,
				 "q6voice_remove() called while path %d is active\n",
				 path);

			if (p->runtime->started == 3)
				q6voice_path_stop(p);
			q6voice_path_destroy(p);
		}
		mutex_unlock(&p->lock);
		mutex_destroy(&p->lock);
	}
}

struct q6voice *q6voice_create(struct device *dev)
{
	struct q6voice *v;
	enum q6voice_path_type path;
	int ret;

	v = devm_kzalloc(dev, sizeof(*v), GFP_KERNEL);
	if (!v)
		return ERR_PTR(-ENOMEM);

	v->dev = dev;

	for (path = 0; path < Q6VOICE_PATH_COUNT; ++path) {
		struct q6voice_path *p = &v->paths[path];

		p->v = v;
		p->type = path;
		mutex_init(&p->lock);
	}

	ret = devm_add_action(dev, q6voice_free, v);
	if (ret)
		return ERR_PTR(ret);

	return v;
}
EXPORT_SYMBOL_GPL(q6voice_create);

int q6voice_get_port(struct q6voice *v, enum q6voice_path_type path,
		     bool capture)
{
	struct q6voice_path *p = &v->paths[path];

	if (capture)
		return p->tx_port;
	else
		return p->rx_port;
}
EXPORT_SYMBOL_GPL(q6voice_get_port);

void q6voice_set_port(struct q6voice *v, enum q6voice_path_type path,
		      bool capture, int index)
{
	struct q6voice_path *p = &v->paths[path];

	if (capture)
		p->tx_port = index;
	else
		p->rx_port = index;
}
EXPORT_SYMBOL_GPL(q6voice_set_port);

MODULE_AUTHOR("Stephan Gerhold <stephan@gerhold.net>");
MODULE_DESCRIPTION("Q6Voice driver");
MODULE_LICENSE("GPL v2");
