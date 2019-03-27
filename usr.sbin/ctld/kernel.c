/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003, 2004 Silicon Graphics International Corp.
 * Copyright (c) 1997-2007 Kenneth D. Merry
 * Copyright (c) 2012 The FreeBSD Foundation
 * Copyright (c) 2017 Jakub Wojciech Klama <jceel@FreeBSD.org>
 * All rights reserved.
 *
 * Portions of this software were developed by Edward Tomasz Napierala
 * under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/capsicum.h>
#include <sys/callout.h>
#include <sys/ioctl.h>
#include <sys/linker.h>
#include <sys/module.h>
#include <sys/queue.h>
#include <sys/sbuf.h>
#include <sys/nv.h>
#include <sys/stat.h>
#include <assert.h>
#include <bsdxml.h>
#include <capsicum_helpers.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_message.h>
#include <cam/ctl/ctl.h>
#include <cam/ctl/ctl_io.h>
#include <cam/ctl/ctl_backend.h>
#include <cam/ctl/ctl_ioctl.h>
#include <cam/ctl/ctl_util.h>
#include <cam/ctl/ctl_scsi_all.h>

#include "ctld.h"

#ifdef ICL_KERNEL_PROXY
#include <netdb.h>
#endif

#define	NVLIST_BUFSIZE	1024

extern bool proxy_mode;

static int	ctl_fd = 0;

void
kernel_init(void)
{
	int retval, saved_errno;

	ctl_fd = open(CTL_DEFAULT_DEV, O_RDWR);
	if (ctl_fd < 0 && errno == ENOENT) {
		saved_errno = errno;
		retval = kldload("ctl");
		if (retval != -1)
			ctl_fd = open(CTL_DEFAULT_DEV, O_RDWR);
		else
			errno = saved_errno;
	}
	if (ctl_fd < 0)
		log_err(1, "failed to open %s", CTL_DEFAULT_DEV);
#ifdef	WANT_ISCSI
	else {
		saved_errno = errno;
		if (modfind("cfiscsi") == -1 && kldload("cfiscsi") == -1)
			log_warn("couldn't load cfiscsi");
		errno = saved_errno;
	}
#endif
}

/*
 * Name/value pair used for per-LUN attributes.
 */
struct cctl_lun_nv {
	char *name;
	char *value;
	STAILQ_ENTRY(cctl_lun_nv) links;
};

/*
 * Backend LUN information.
 */
struct cctl_lun {
	uint64_t lun_id;
	char *backend_type;
	uint8_t device_type;
	uint64_t size_blocks;
	uint32_t blocksize;
	char *serial_number;
	char *device_id;
	char *ctld_name;
	STAILQ_HEAD(,cctl_lun_nv) attr_list;
	STAILQ_ENTRY(cctl_lun) links;
};

struct cctl_port {
	uint32_t port_id;
	char *port_frontend;
	char *port_name;
	int pp;
	int vp;
	int cfiscsi_state;
	char *cfiscsi_target;
	uint16_t cfiscsi_portal_group_tag;
	char *ctld_portal_group_name;
	STAILQ_HEAD(,cctl_lun_nv) attr_list;
	STAILQ_ENTRY(cctl_port) links;
};

struct cctl_devlist_data {
	int num_luns;
	STAILQ_HEAD(,cctl_lun) lun_list;
	struct cctl_lun *cur_lun;
	int num_ports;
	STAILQ_HEAD(,cctl_port) port_list;
	struct cctl_port *cur_port;
	int level;
	struct sbuf *cur_sb[32];
};

static void
cctl_start_element(void *user_data, const char *name, const char **attr)
{
	int i;
	struct cctl_devlist_data *devlist;
	struct cctl_lun *cur_lun;

	devlist = (struct cctl_devlist_data *)user_data;
	cur_lun = devlist->cur_lun;
	devlist->level++;
	if ((u_int)devlist->level >= (sizeof(devlist->cur_sb) /
	    sizeof(devlist->cur_sb[0])))
		log_errx(1, "%s: too many nesting levels, %zd max", __func__,
		     sizeof(devlist->cur_sb) / sizeof(devlist->cur_sb[0]));

	devlist->cur_sb[devlist->level] = sbuf_new_auto();
	if (devlist->cur_sb[devlist->level] == NULL)
		log_err(1, "%s: unable to allocate sbuf", __func__);

	if (strcmp(name, "lun") == 0) {
		if (cur_lun != NULL)
			log_errx(1, "%s: improper lun element nesting",
			    __func__);

		cur_lun = calloc(1, sizeof(*cur_lun));
		if (cur_lun == NULL)
			log_err(1, "%s: cannot allocate %zd bytes", __func__,
			    sizeof(*cur_lun));

		devlist->num_luns++;
		devlist->cur_lun = cur_lun;

		STAILQ_INIT(&cur_lun->attr_list);
		STAILQ_INSERT_TAIL(&devlist->lun_list, cur_lun, links);

		for (i = 0; attr[i] != NULL; i += 2) {
			if (strcmp(attr[i], "id") == 0) {
				cur_lun->lun_id = strtoull(attr[i+1], NULL, 0);
			} else {
				log_errx(1, "%s: invalid LUN attribute %s = %s",
				     __func__, attr[i], attr[i+1]);
			}
		}
	}
}

static void
cctl_end_element(void *user_data, const char *name)
{
	struct cctl_devlist_data *devlist;
	struct cctl_lun *cur_lun;
	char *str;

	devlist = (struct cctl_devlist_data *)user_data;
	cur_lun = devlist->cur_lun;

	if ((cur_lun == NULL)
	 && (strcmp(name, "ctllunlist") != 0))
		log_errx(1, "%s: cur_lun == NULL! (name = %s)", __func__, name);

	if (devlist->cur_sb[devlist->level] == NULL)
		log_errx(1, "%s: no valid sbuf at level %d (name %s)", __func__,
		     devlist->level, name);

	sbuf_finish(devlist->cur_sb[devlist->level]);
	str = checked_strdup(sbuf_data(devlist->cur_sb[devlist->level]));

	if (strlen(str) == 0) {
		free(str);
		str = NULL;
	}

	sbuf_delete(devlist->cur_sb[devlist->level]);
	devlist->cur_sb[devlist->level] = NULL;
	devlist->level--;

	if (strcmp(name, "backend_type") == 0) {
		cur_lun->backend_type = str;
		str = NULL;
	} else if (strcmp(name, "lun_type") == 0) {
		cur_lun->device_type = strtoull(str, NULL, 0);
	} else if (strcmp(name, "size") == 0) {
		cur_lun->size_blocks = strtoull(str, NULL, 0);
	} else if (strcmp(name, "blocksize") == 0) {
		cur_lun->blocksize = strtoul(str, NULL, 0);
	} else if (strcmp(name, "serial_number") == 0) {
		cur_lun->serial_number = str;
		str = NULL;
	} else if (strcmp(name, "device_id") == 0) {
		cur_lun->device_id = str;
		str = NULL;
	} else if (strcmp(name, "ctld_name") == 0) {
		cur_lun->ctld_name = str;
		str = NULL;
	} else if (strcmp(name, "lun") == 0) {
		devlist->cur_lun = NULL;
	} else if (strcmp(name, "ctllunlist") == 0) {
		/* Nothing. */
	} else {
		struct cctl_lun_nv *nv;

		nv = calloc(1, sizeof(*nv));
		if (nv == NULL)
			log_err(1, "%s: can't allocate %zd bytes for nv pair",
			    __func__, sizeof(*nv));

		nv->name = checked_strdup(name);

		nv->value = str;
		str = NULL;
		STAILQ_INSERT_TAIL(&cur_lun->attr_list, nv, links);
	}

	free(str);
}

static void
cctl_start_pelement(void *user_data, const char *name, const char **attr)
{
	int i;
	struct cctl_devlist_data *devlist;
	struct cctl_port *cur_port;

	devlist = (struct cctl_devlist_data *)user_data;
	cur_port = devlist->cur_port;
	devlist->level++;
	if ((u_int)devlist->level >= (sizeof(devlist->cur_sb) /
	    sizeof(devlist->cur_sb[0])))
		log_errx(1, "%s: too many nesting levels, %zd max", __func__,
		     sizeof(devlist->cur_sb) / sizeof(devlist->cur_sb[0]));

	devlist->cur_sb[devlist->level] = sbuf_new_auto();
	if (devlist->cur_sb[devlist->level] == NULL)
		log_err(1, "%s: unable to allocate sbuf", __func__);

	if (strcmp(name, "targ_port") == 0) {
		if (cur_port != NULL)
			log_errx(1, "%s: improper port element nesting (%s)",
			    __func__, name);

		cur_port = calloc(1, sizeof(*cur_port));
		if (cur_port == NULL)
			log_err(1, "%s: cannot allocate %zd bytes", __func__,
			    sizeof(*cur_port));

		devlist->num_ports++;
		devlist->cur_port = cur_port;

		STAILQ_INIT(&cur_port->attr_list);
		STAILQ_INSERT_TAIL(&devlist->port_list, cur_port, links);

		for (i = 0; attr[i] != NULL; i += 2) {
			if (strcmp(attr[i], "id") == 0) {
				cur_port->port_id = strtoul(attr[i+1], NULL, 0);
			} else {
				log_errx(1, "%s: invalid LUN attribute %s = %s",
				     __func__, attr[i], attr[i+1]);
			}
		}
	}
}

static void
cctl_end_pelement(void *user_data, const char *name)
{
	struct cctl_devlist_data *devlist;
	struct cctl_port *cur_port;
	char *str;

	devlist = (struct cctl_devlist_data *)user_data;
	cur_port = devlist->cur_port;

	if ((cur_port == NULL)
	 && (strcmp(name, "ctlportlist") != 0))
		log_errx(1, "%s: cur_port == NULL! (name = %s)", __func__, name);

	if (devlist->cur_sb[devlist->level] == NULL)
		log_errx(1, "%s: no valid sbuf at level %d (name %s)", __func__,
		     devlist->level, name);

	sbuf_finish(devlist->cur_sb[devlist->level]);
	str = checked_strdup(sbuf_data(devlist->cur_sb[devlist->level]));

	if (strlen(str) == 0) {
		free(str);
		str = NULL;
	}

	sbuf_delete(devlist->cur_sb[devlist->level]);
	devlist->cur_sb[devlist->level] = NULL;
	devlist->level--;

	if (strcmp(name, "frontend_type") == 0) {
		cur_port->port_frontend = str;
		str = NULL;
	} else if (strcmp(name, "port_name") == 0) {
		cur_port->port_name = str;
		str = NULL;
	} else if (strcmp(name, "physical_port") == 0) {
		cur_port->pp = strtoul(str, NULL, 0);
	} else if (strcmp(name, "virtual_port") == 0) {
		cur_port->vp = strtoul(str, NULL, 0);
	} else if (strcmp(name, "cfiscsi_target") == 0) {
		cur_port->cfiscsi_target = str;
		str = NULL;
	} else if (strcmp(name, "cfiscsi_state") == 0) {
		cur_port->cfiscsi_state = strtoul(str, NULL, 0);
	} else if (strcmp(name, "cfiscsi_portal_group_tag") == 0) {
		cur_port->cfiscsi_portal_group_tag = strtoul(str, NULL, 0);
	} else if (strcmp(name, "ctld_portal_group_name") == 0) {
		cur_port->ctld_portal_group_name = str;
		str = NULL;
	} else if (strcmp(name, "targ_port") == 0) {
		devlist->cur_port = NULL;
	} else if (strcmp(name, "ctlportlist") == 0) {
		/* Nothing. */
	} else {
		struct cctl_lun_nv *nv;

		nv = calloc(1, sizeof(*nv));
		if (nv == NULL)
			log_err(1, "%s: can't allocate %zd bytes for nv pair",
			    __func__, sizeof(*nv));

		nv->name = checked_strdup(name);

		nv->value = str;
		str = NULL;
		STAILQ_INSERT_TAIL(&cur_port->attr_list, nv, links);
	}

	free(str);
}

static void
cctl_char_handler(void *user_data, const XML_Char *str, int len)
{
	struct cctl_devlist_data *devlist;

	devlist = (struct cctl_devlist_data *)user_data;

	sbuf_bcat(devlist->cur_sb[devlist->level], str, len);
}

struct conf *
conf_new_from_kernel(void)
{
	struct conf *conf = NULL;
	struct target *targ;
	struct portal_group *pg;
	struct pport *pp;
	struct port *cp;
	struct lun *cl;
	struct option *o;
	struct ctl_lun_list list;
	struct cctl_devlist_data devlist;
	struct cctl_lun *lun;
	struct cctl_port *port;
	XML_Parser parser;
	char *str, *name;
	int len, retval;

	bzero(&devlist, sizeof(devlist));
	STAILQ_INIT(&devlist.lun_list);
	STAILQ_INIT(&devlist.port_list);

	log_debugx("obtaining previously configured CTL luns from the kernel");

	str = NULL;
	len = 4096;
retry:
	str = realloc(str, len);
	if (str == NULL)
		log_err(1, "realloc");

	bzero(&list, sizeof(list));
	list.alloc_len = len;
	list.status = CTL_LUN_LIST_NONE;
	list.lun_xml = str;

	if (ioctl(ctl_fd, CTL_LUN_LIST, &list) == -1) {
		log_warn("error issuing CTL_LUN_LIST ioctl");
		free(str);
		return (NULL);
	}

	if (list.status == CTL_LUN_LIST_ERROR) {
		log_warnx("error returned from CTL_LUN_LIST ioctl: %s",
		    list.error_str);
		free(str);
		return (NULL);
	}

	if (list.status == CTL_LUN_LIST_NEED_MORE_SPACE) {
		len = len << 1;
		goto retry;
	}

	parser = XML_ParserCreate(NULL);
	if (parser == NULL) {
		log_warnx("unable to create XML parser");
		free(str);
		return (NULL);
	}

	XML_SetUserData(parser, &devlist);
	XML_SetElementHandler(parser, cctl_start_element, cctl_end_element);
	XML_SetCharacterDataHandler(parser, cctl_char_handler);

	retval = XML_Parse(parser, str, strlen(str), 1);
	XML_ParserFree(parser);
	free(str);
	if (retval != 1) {
		log_warnx("XML_Parse failed");
		return (NULL);
	}

	str = NULL;
	len = 4096;
retry_port:
	str = realloc(str, len);
	if (str == NULL)
		log_err(1, "realloc");

	bzero(&list, sizeof(list));
	list.alloc_len = len;
	list.status = CTL_LUN_LIST_NONE;
	list.lun_xml = str;

	if (ioctl(ctl_fd, CTL_PORT_LIST, &list) == -1) {
		log_warn("error issuing CTL_PORT_LIST ioctl");
		free(str);
		return (NULL);
	}

	if (list.status == CTL_LUN_LIST_ERROR) {
		log_warnx("error returned from CTL_PORT_LIST ioctl: %s",
		    list.error_str);
		free(str);
		return (NULL);
	}

	if (list.status == CTL_LUN_LIST_NEED_MORE_SPACE) {
		len = len << 1;
		goto retry_port;
	}

	parser = XML_ParserCreate(NULL);
	if (parser == NULL) {
		log_warnx("unable to create XML parser");
		free(str);
		return (NULL);
	}

	XML_SetUserData(parser, &devlist);
	XML_SetElementHandler(parser, cctl_start_pelement, cctl_end_pelement);
	XML_SetCharacterDataHandler(parser, cctl_char_handler);

	retval = XML_Parse(parser, str, strlen(str), 1);
	XML_ParserFree(parser);
	free(str);
	if (retval != 1) {
		log_warnx("XML_Parse failed");
		return (NULL);
	}

	conf = conf_new();

	name = NULL;
	STAILQ_FOREACH(port, &devlist.port_list, links) {
		if (strcmp(port->port_frontend, "ha") == 0)
			continue;
		free(name);
		if (port->pp == 0 && port->vp == 0) {
			name = checked_strdup(port->port_name);
		} else if (port->vp == 0) {
			retval = asprintf(&name, "%s/%d",
			    port->port_name, port->pp);
			if (retval <= 0)
				log_err(1, "asprintf");
		} else {
			retval = asprintf(&name, "%s/%d/%d",
			    port->port_name, port->pp, port->vp);
			if (retval <= 0)
				log_err(1, "asprintf");
		}

		if (port->cfiscsi_target == NULL) {
			log_debugx("CTL port %u \"%s\" wasn't managed by ctld; ",
			    port->port_id, name);
			pp = pport_find(conf, name);
			if (pp == NULL) {
#if 0
				log_debugx("found new kernel port %u \"%s\"",
				    port->port_id, name);
#endif
				pp = pport_new(conf, name, port->port_id);
				if (pp == NULL) {
					log_warnx("pport_new failed");
					continue;
				}
			}
			continue;
		}
		if (port->cfiscsi_state != 1) {
			log_debugx("CTL port %ju is not active (%d); ignoring",
			    (uintmax_t)port->port_id, port->cfiscsi_state);
			continue;
		}

		targ = target_find(conf, port->cfiscsi_target);
		if (targ == NULL) {
#if 0
			log_debugx("found new kernel target %s for CTL port %ld",
			    port->cfiscsi_target, port->port_id);
#endif
			targ = target_new(conf, port->cfiscsi_target);
			if (targ == NULL) {
				log_warnx("target_new failed");
				continue;
			}
		}

		if (port->ctld_portal_group_name == NULL)
			continue;
		pg = portal_group_find(conf, port->ctld_portal_group_name);
		if (pg == NULL) {
#if 0
			log_debugx("found new kernel portal group %s for CTL port %ld",
			    port->ctld_portal_group_name, port->port_id);
#endif
			pg = portal_group_new(conf, port->ctld_portal_group_name);
			if (pg == NULL) {
				log_warnx("portal_group_new failed");
				continue;
			}
		}
		pg->pg_tag = port->cfiscsi_portal_group_tag;
		cp = port_new(conf, targ, pg);
		if (cp == NULL) {
			log_warnx("port_new failed");
			continue;
		}
		cp->p_ctl_port = port->port_id;
	}
	free(name);

	STAILQ_FOREACH(lun, &devlist.lun_list, links) {
		struct cctl_lun_nv *nv;

		if (lun->ctld_name == NULL) {
			log_debugx("CTL lun %ju wasn't managed by ctld; "
			    "ignoring", (uintmax_t)lun->lun_id);
			continue;
		}

		cl = lun_find(conf, lun->ctld_name);
		if (cl != NULL) {
			log_warnx("found CTL lun %ju \"%s\", "
			    "also backed by CTL lun %d; ignoring",
			    (uintmax_t)lun->lun_id, lun->ctld_name,
			    cl->l_ctl_lun);
			continue;
		}

		log_debugx("found CTL lun %ju \"%s\"",
		    (uintmax_t)lun->lun_id, lun->ctld_name);

		cl = lun_new(conf, lun->ctld_name);
		if (cl == NULL) {
			log_warnx("lun_new failed");
			continue;
		}
		lun_set_backend(cl, lun->backend_type);
		lun_set_device_type(cl, lun->device_type);
		lun_set_blocksize(cl, lun->blocksize);
		lun_set_device_id(cl, lun->device_id);
		lun_set_serial(cl, lun->serial_number);
		lun_set_size(cl, lun->size_blocks * cl->l_blocksize);
		lun_set_ctl_lun(cl, lun->lun_id);

		STAILQ_FOREACH(nv, &lun->attr_list, links) {
			if (strcmp(nv->name, "file") == 0 ||
			    strcmp(nv->name, "dev") == 0) {
				lun_set_path(cl, nv->value);
				continue;
			}
			o = option_new(&cl->l_options, nv->name, nv->value);
			if (o == NULL)
				log_warnx("unable to add CTL lun option %s "
				    "for CTL lun %ju \"%s\"",
				    nv->name, (uintmax_t) lun->lun_id,
				    cl->l_name);
		}
	}

	return (conf);
}

int
kernel_lun_add(struct lun *lun)
{
	struct option *o;
	struct ctl_lun_req req;
	int error;

	bzero(&req, sizeof(req));

	strlcpy(req.backend, lun->l_backend, sizeof(req.backend));
	req.reqtype = CTL_LUNREQ_CREATE;

	req.reqdata.create.blocksize_bytes = lun->l_blocksize;

	if (lun->l_size != 0)
		req.reqdata.create.lun_size_bytes = lun->l_size;

	if (lun->l_ctl_lun >= 0) {
		req.reqdata.create.req_lun_id = lun->l_ctl_lun;
		req.reqdata.create.flags |= CTL_LUN_FLAG_ID_REQ;
	}

	req.reqdata.create.flags |= CTL_LUN_FLAG_DEV_TYPE;
	req.reqdata.create.device_type = lun->l_device_type;

	if (lun->l_serial != NULL) {
		strncpy(req.reqdata.create.serial_num, lun->l_serial,
			sizeof(req.reqdata.create.serial_num));
		req.reqdata.create.flags |= CTL_LUN_FLAG_SERIAL_NUM;
	}

	if (lun->l_device_id != NULL) {
		strncpy(req.reqdata.create.device_id, lun->l_device_id,
			sizeof(req.reqdata.create.device_id));
		req.reqdata.create.flags |= CTL_LUN_FLAG_DEVID;
	}

	if (lun->l_path != NULL) {
		o = option_find(&lun->l_options, "file");
		if (o != NULL) {
			option_set(o, lun->l_path);
		} else {
			o = option_new(&lun->l_options, "file", lun->l_path);
			assert(o != NULL);
		}
	}

	o = option_find(&lun->l_options, "ctld_name");
	if (o != NULL) {
		option_set(o, lun->l_name);
	} else {
		o = option_new(&lun->l_options, "ctld_name", lun->l_name);
		assert(o != NULL);
	}

	o = option_find(&lun->l_options, "scsiname");
	if (o == NULL && lun->l_scsiname != NULL) {
		o = option_new(&lun->l_options, "scsiname", lun->l_scsiname);
		assert(o != NULL);
	}

	if (!TAILQ_EMPTY(&lun->l_options)) {
		req.args_nvl = nvlist_create(0);
		if (req.args_nvl == NULL) {
			log_warn("error allocating nvlist");
			return (1);
		}

		TAILQ_FOREACH(o, &lun->l_options, o_next)
			nvlist_add_string(req.args_nvl, o->o_name, o->o_value);

		req.args = nvlist_pack(req.args_nvl, &req.args_len);
		if (req.args == NULL) {
			log_warn("error packing nvlist");
			return (1);
		}
	}

	error = ioctl(ctl_fd, CTL_LUN_REQ, &req);
	nvlist_destroy(req.args_nvl);

	if (error != 0) {
		log_warn("error issuing CTL_LUN_REQ ioctl");
		return (1);
	}

	switch (req.status) {
	case CTL_LUN_ERROR:
		log_warnx("LUN creation error: %s", req.error_str);
		return (1);
	case CTL_LUN_WARNING:
		log_warnx("LUN creation warning: %s", req.error_str);
		break;
	case CTL_LUN_OK:
		break;
	default:
		log_warnx("unknown LUN creation status: %d",
		    req.status);
		return (1);
	}

	lun_set_ctl_lun(lun, req.reqdata.create.req_lun_id);
	return (0);
}

int
kernel_lun_modify(struct lun *lun)
{
	struct option *o;
	struct ctl_lun_req req;
	int error;

	bzero(&req, sizeof(req));

	strlcpy(req.backend, lun->l_backend, sizeof(req.backend));
	req.reqtype = CTL_LUNREQ_MODIFY;

	req.reqdata.modify.lun_id = lun->l_ctl_lun;
	req.reqdata.modify.lun_size_bytes = lun->l_size;

	if (!TAILQ_EMPTY(&lun->l_options)) {
		req.args_nvl = nvlist_create(0);
		if (req.args_nvl == NULL) {
			log_warn("error allocating nvlist");
			return (1);
		}

		TAILQ_FOREACH(o, &lun->l_options, o_next)
			nvlist_add_string(req.args_nvl, o->o_name, o->o_value);

		req.args = nvlist_pack(req.args_nvl, &req.args_len);
		if (req.args == NULL) {
			log_warn("error packing nvlist");
			return (1);
		}
	}

	error = ioctl(ctl_fd, CTL_LUN_REQ, &req);
	nvlist_destroy(req.args_nvl);

	if (error != 0) {
		log_warn("error issuing CTL_LUN_REQ ioctl");
		return (1);
	}

	switch (req.status) {
	case CTL_LUN_ERROR:
		log_warnx("LUN modification error: %s", req.error_str);
		return (1);
	case CTL_LUN_WARNING:
		log_warnx("LUN modification warning: %s", req.error_str);
		break;
	case CTL_LUN_OK:
		break;
	default:
		log_warnx("unknown LUN modification status: %d",
		    req.status);
		return (1);
	}

	return (0);
}

int
kernel_lun_remove(struct lun *lun)
{
	struct ctl_lun_req req;

	bzero(&req, sizeof(req));

	strlcpy(req.backend, lun->l_backend, sizeof(req.backend));
	req.reqtype = CTL_LUNREQ_RM;

	req.reqdata.rm.lun_id = lun->l_ctl_lun;

	if (ioctl(ctl_fd, CTL_LUN_REQ, &req) == -1) {
		log_warn("error issuing CTL_LUN_REQ ioctl");
		return (1);
	}

	switch (req.status) {
	case CTL_LUN_ERROR:
		log_warnx("LUN removal error: %s", req.error_str);
		return (1);
	case CTL_LUN_WARNING:
		log_warnx("LUN removal warning: %s", req.error_str);
		break;
	case CTL_LUN_OK:
		break;
	default:
		log_warnx("unknown LUN removal status: %d", req.status);
		return (1);
	}

	return (0);
}

void
kernel_handoff(struct connection *conn)
{
	struct ctl_iscsi req;

	bzero(&req, sizeof(req));

	req.type = CTL_ISCSI_HANDOFF;
	strlcpy(req.data.handoff.initiator_name,
	    conn->conn_initiator_name, sizeof(req.data.handoff.initiator_name));
	strlcpy(req.data.handoff.initiator_addr,
	    conn->conn_initiator_addr, sizeof(req.data.handoff.initiator_addr));
	if (conn->conn_initiator_alias != NULL) {
		strlcpy(req.data.handoff.initiator_alias,
		    conn->conn_initiator_alias, sizeof(req.data.handoff.initiator_alias));
	}
	memcpy(req.data.handoff.initiator_isid, conn->conn_initiator_isid,
	    sizeof(req.data.handoff.initiator_isid));
	strlcpy(req.data.handoff.target_name,
	    conn->conn_target->t_name, sizeof(req.data.handoff.target_name));
	if (conn->conn_portal->p_portal_group->pg_offload != NULL) {
		strlcpy(req.data.handoff.offload,
		    conn->conn_portal->p_portal_group->pg_offload,
		    sizeof(req.data.handoff.offload));
	}
#ifdef ICL_KERNEL_PROXY
	if (proxy_mode)
		req.data.handoff.connection_id = conn->conn_socket;
	else
		req.data.handoff.socket = conn->conn_socket;
#else
	req.data.handoff.socket = conn->conn_socket;
#endif
	req.data.handoff.portal_group_tag =
	    conn->conn_portal->p_portal_group->pg_tag;
	if (conn->conn_header_digest == CONN_DIGEST_CRC32C)
		req.data.handoff.header_digest = CTL_ISCSI_DIGEST_CRC32C;
	if (conn->conn_data_digest == CONN_DIGEST_CRC32C)
		req.data.handoff.data_digest = CTL_ISCSI_DIGEST_CRC32C;
	req.data.handoff.cmdsn = conn->conn_cmdsn;
	req.data.handoff.statsn = conn->conn_statsn;
	req.data.handoff.max_recv_data_segment_length =
	    conn->conn_max_recv_data_segment_length;
	req.data.handoff.max_send_data_segment_length =
	    conn->conn_max_send_data_segment_length;
	req.data.handoff.max_burst_length = conn->conn_max_burst_length;
	req.data.handoff.first_burst_length = conn->conn_first_burst_length;
	req.data.handoff.immediate_data = conn->conn_immediate_data;

	if (ioctl(ctl_fd, CTL_ISCSI, &req) == -1) {
		log_err(1, "error issuing CTL_ISCSI ioctl; "
		    "dropping connection");
	}

	if (req.status != CTL_ISCSI_OK) {
		log_errx(1, "error returned from CTL iSCSI handoff request: "
		    "%s; dropping connection", req.error_str);
	}
}

void
kernel_limits(const char *offload, int *max_recv_dsl, int *max_send_dsl,
    int *max_burst_length, int *first_burst_length)
{
	struct ctl_iscsi req;
	struct ctl_iscsi_limits_params *cilp;

	bzero(&req, sizeof(req));

	req.type = CTL_ISCSI_LIMITS;
	cilp = (struct ctl_iscsi_limits_params *)&(req.data.limits);
	if (offload != NULL) {
		strlcpy(cilp->offload, offload, sizeof(cilp->offload));
	}

	if (ioctl(ctl_fd, CTL_ISCSI, &req) == -1) {
		log_err(1, "error issuing CTL_ISCSI ioctl; "
		    "dropping connection");
	}

	if (req.status != CTL_ISCSI_OK) {
		log_errx(1, "error returned from CTL iSCSI limits request: "
		    "%s; dropping connection", req.error_str);
	}

	if (cilp->max_recv_data_segment_length != 0) {
		*max_recv_dsl = cilp->max_recv_data_segment_length;
		*max_send_dsl = cilp->max_recv_data_segment_length;
	}
	if (cilp->max_send_data_segment_length != 0)
		*max_send_dsl = cilp->max_send_data_segment_length;
	if (cilp->max_burst_length != 0)
		*max_burst_length = cilp->max_burst_length;
	if (cilp->first_burst_length != 0)
		*first_burst_length = cilp->first_burst_length;
	if (*max_burst_length < *first_burst_length)
		*first_burst_length = *max_burst_length;

	if (offload != NULL) {
		log_debugx("Kernel limits for offload \"%s\" are "
		    "MaxRecvDataSegment=%d, max_send_dsl=%d, "
		    "MaxBurstLength=%d, FirstBurstLength=%d",
		    offload, *max_recv_dsl, *max_send_dsl, *max_burst_length,
		    *first_burst_length);
	} else {
		log_debugx("Kernel limits are "
		    "MaxRecvDataSegment=%d, max_send_dsl=%d, "
		    "MaxBurstLength=%d, FirstBurstLength=%d",
		    *max_recv_dsl, *max_send_dsl, *max_burst_length,
		    *first_burst_length);
	}
}

int
kernel_port_add(struct port *port)
{
	struct option *o;
	struct ctl_port_entry entry;
	struct ctl_req req;
	struct ctl_lun_map lm;
	struct target *targ = port->p_target;
	struct portal_group *pg = port->p_portal_group;
	char result_buf[NVLIST_BUFSIZE];
	int error, i;

	/* Create iSCSI port. */
	if (port->p_portal_group || port->p_ioctl_port) {
		bzero(&req, sizeof(req));
		req.reqtype = CTL_REQ_CREATE;

		if (port->p_portal_group) {
			strlcpy(req.driver, "iscsi", sizeof(req.driver));
			req.args_nvl = nvlist_create(0);
			nvlist_add_string(req.args_nvl, "cfiscsi_target",
			    targ->t_name);
			nvlist_add_string(req.args_nvl,
			    "ctld_portal_group_name", pg->pg_name);
			nvlist_add_stringf(req.args_nvl,
			    "cfiscsi_portal_group_tag", "%u", pg->pg_tag);

			if (targ->t_alias) {
				nvlist_add_string(req.args_nvl,
				    "cfiscsi_target_alias", targ->t_alias);
			}

			TAILQ_FOREACH(o, &pg->pg_options, o_next)
				nvlist_add_string(req.args_nvl, o->o_name,
				    o->o_value);
		}

		if (port->p_ioctl_port) {
			strlcpy(req.driver, "ioctl", sizeof(req.driver));
			req.args_nvl = nvlist_create(0);
			nvlist_add_stringf(req.args_nvl, "pp", "%d",
			    port->p_ioctl_pp);
			nvlist_add_stringf(req.args_nvl, "vp", "%d",
			    port->p_ioctl_vp);
		}

		req.args = nvlist_pack(req.args_nvl, &req.args_len);
		if (req.args == NULL) {
			log_warn("error packing nvlist");
			return (1);
		}

		req.result = result_buf;
		req.result_len = sizeof(result_buf);
		error = ioctl(ctl_fd, CTL_PORT_REQ, &req);
		nvlist_destroy(req.args_nvl);

		if (error != 0) {
			log_warn("error issuing CTL_PORT_REQ ioctl");
			return (1);
		}
		if (req.status == CTL_LUN_ERROR) {
			log_warnx("error returned from port creation request: %s",
			    req.error_str);
			return (1);
		}
		if (req.status != CTL_LUN_OK) {
			log_warnx("unknown port creation request status %d",
			    req.status);
			return (1);
		}

		req.result_nvl = nvlist_unpack(result_buf, req.result_len, 0);
		if (req.result_nvl == NULL) {
			log_warnx("error unpacking result nvlist");
			return (1);
		}

		port->p_ctl_port = nvlist_get_number(req.result_nvl, "port_id");
		nvlist_destroy(req.result_nvl);
	} else if (port->p_pport) {
		port->p_ctl_port = port->p_pport->pp_ctl_port;

		if (strncmp(targ->t_name, "naa.", 4) == 0 &&
		    strlen(targ->t_name) == 20) {
			bzero(&entry, sizeof(entry));
			entry.port_type = CTL_PORT_NONE;
			entry.targ_port = port->p_ctl_port;
			entry.flags |= CTL_PORT_WWNN_VALID;
			entry.wwnn = strtoull(targ->t_name + 4, NULL, 16);
			if (ioctl(ctl_fd, CTL_SET_PORT_WWNS, &entry) == -1)
				log_warn("CTL_SET_PORT_WWNS ioctl failed");
		}
	}

	/* Explicitly enable mapping to block any access except allowed. */
	lm.port = port->p_ctl_port;
	lm.plun = UINT32_MAX;
	lm.lun = 0;
	error = ioctl(ctl_fd, CTL_LUN_MAP, &lm);
	if (error != 0)
		log_warn("CTL_LUN_MAP ioctl failed");

	/* Map configured LUNs */
	for (i = 0; i < MAX_LUNS; i++) {
		if (targ->t_luns[i] == NULL)
			continue;
		lm.port = port->p_ctl_port;
		lm.plun = i;
		lm.lun = targ->t_luns[i]->l_ctl_lun;
		error = ioctl(ctl_fd, CTL_LUN_MAP, &lm);
		if (error != 0)
			log_warn("CTL_LUN_MAP ioctl failed");
	}

	/* Enable port */
	bzero(&entry, sizeof(entry));
	entry.targ_port = port->p_ctl_port;
	error = ioctl(ctl_fd, CTL_ENABLE_PORT, &entry);
	if (error != 0) {
		log_warn("CTL_ENABLE_PORT ioctl failed");
		return (-1);
	}

	return (0);
}

int
kernel_port_update(struct port *port, struct port *oport)
{
	struct ctl_lun_map lm;
	struct target *targ = port->p_target;
	struct target *otarg = oport->p_target;
	int error, i;
	uint32_t olun;

	/* Map configured LUNs and unmap others */
	for (i = 0; i < MAX_LUNS; i++) {
		lm.port = port->p_ctl_port;
		lm.plun = i;
		if (targ->t_luns[i] == NULL)
			lm.lun = UINT32_MAX;
		else
			lm.lun = targ->t_luns[i]->l_ctl_lun;
		if (otarg->t_luns[i] == NULL)
			olun = UINT32_MAX;
		else
			olun = otarg->t_luns[i]->l_ctl_lun;
		if (lm.lun == olun)
			continue;
		error = ioctl(ctl_fd, CTL_LUN_MAP, &lm);
		if (error != 0)
			log_warn("CTL_LUN_MAP ioctl failed");
	}
	return (0);
}

int
kernel_port_remove(struct port *port)
{
	struct ctl_port_entry entry;
	struct ctl_lun_map lm;
	struct ctl_req req;
	struct target *targ = port->p_target;
	struct portal_group *pg = port->p_portal_group;
	int error;

	/* Disable port */
	bzero(&entry, sizeof(entry));
	entry.targ_port = port->p_ctl_port;
	error = ioctl(ctl_fd, CTL_DISABLE_PORT, &entry);
	if (error != 0) {
		log_warn("CTL_DISABLE_PORT ioctl failed");
		return (-1);
	}

	/* Remove iSCSI or ioctl port. */
	if (port->p_portal_group || port->p_ioctl_port) {
		bzero(&req, sizeof(req));
		strlcpy(req.driver, port->p_ioctl_port ? "ioctl" : "iscsi",
		    sizeof(req.driver));
		req.reqtype = CTL_REQ_REMOVE;
		req.args_nvl = nvlist_create(0);
		if (req.args_nvl == NULL)
			log_err(1, "nvlist_create");

		if (port->p_ioctl_port)
			nvlist_add_stringf(req.args_nvl, "port_id", "%d",
			    port->p_ctl_port);
		else {
			nvlist_add_string(req.args_nvl, "cfiscsi_target",
			    targ->t_name);
			nvlist_add_stringf(req.args_nvl,
			    "cfiscsi_portal_group_tag", "%u", pg->pg_tag);
		}

		req.args = nvlist_pack(req.args_nvl, &req.args_len);
		if (req.args == NULL) {
			log_warn("error packing nvlist");
			return (1);
		}

		error = ioctl(ctl_fd, CTL_PORT_REQ, &req);
		nvlist_destroy(req.args_nvl);

		if (error != 0) {
			log_warn("error issuing CTL_PORT_REQ ioctl");
			return (1);
		}
		if (req.status == CTL_LUN_ERROR) {
			log_warnx("error returned from port removal request: %s",
			    req.error_str);
			return (1);
		}
		if (req.status != CTL_LUN_OK) {
			log_warnx("unknown port removal request status %d",
			    req.status);
			return (1);
		}
	} else {
		/* Disable LUN mapping. */
		lm.port = port->p_ctl_port;
		lm.plun = UINT32_MAX;
		lm.lun = UINT32_MAX;
		error = ioctl(ctl_fd, CTL_LUN_MAP, &lm);
		if (error != 0)
			log_warn("CTL_LUN_MAP ioctl failed");
	}
	return (0);
}

#ifdef ICL_KERNEL_PROXY
void
kernel_listen(struct addrinfo *ai, bool iser, int portal_id)
{
	struct ctl_iscsi req;

	bzero(&req, sizeof(req));

	req.type = CTL_ISCSI_LISTEN;
	req.data.listen.iser = iser;
	req.data.listen.domain = ai->ai_family;
	req.data.listen.socktype = ai->ai_socktype;
	req.data.listen.protocol = ai->ai_protocol;
	req.data.listen.addr = ai->ai_addr;
	req.data.listen.addrlen = ai->ai_addrlen;
	req.data.listen.portal_id = portal_id;

	if (ioctl(ctl_fd, CTL_ISCSI, &req) == -1)
		log_err(1, "error issuing CTL_ISCSI ioctl");

	if (req.status != CTL_ISCSI_OK) {
		log_errx(1, "error returned from CTL iSCSI listen: %s",
		    req.error_str);
	}
}

void
kernel_accept(int *connection_id, int *portal_id,
    struct sockaddr *client_sa, socklen_t *client_salen)
{
	struct ctl_iscsi req;
	struct sockaddr_storage ss;

	bzero(&req, sizeof(req));

	req.type = CTL_ISCSI_ACCEPT;
	req.data.accept.initiator_addr = (struct sockaddr *)&ss;

	if (ioctl(ctl_fd, CTL_ISCSI, &req) == -1)
		log_err(1, "error issuing CTL_ISCSI ioctl");

	if (req.status != CTL_ISCSI_OK) {
		log_errx(1, "error returned from CTL iSCSI accept: %s",
		    req.error_str);
	}

	*connection_id = req.data.accept.connection_id;
	*portal_id = req.data.accept.portal_id;
	*client_salen = req.data.accept.initiator_addrlen;
	memcpy(client_sa, &ss, *client_salen);
}

void
kernel_send(struct pdu *pdu)
{
	struct ctl_iscsi req;

	bzero(&req, sizeof(req));

	req.type = CTL_ISCSI_SEND;
	req.data.send.connection_id = pdu->pdu_connection->conn_socket;
	req.data.send.bhs = pdu->pdu_bhs;
	req.data.send.data_segment_len = pdu->pdu_data_len;
	req.data.send.data_segment = pdu->pdu_data;

	if (ioctl(ctl_fd, CTL_ISCSI, &req) == -1) {
		log_err(1, "error issuing CTL_ISCSI ioctl; "
		    "dropping connection");
	}

	if (req.status != CTL_ISCSI_OK) {
		log_errx(1, "error returned from CTL iSCSI send: "
		    "%s; dropping connection", req.error_str);
	}
}

void
kernel_receive(struct pdu *pdu)
{
	struct connection *conn;
	struct ctl_iscsi req;

	conn = pdu->pdu_connection;
	pdu->pdu_data = malloc(conn->conn_max_recv_data_segment_length);
	if (pdu->pdu_data == NULL)
		log_err(1, "malloc");

	bzero(&req, sizeof(req));

	req.type = CTL_ISCSI_RECEIVE;
	req.data.receive.connection_id = conn->conn_socket;
	req.data.receive.bhs = pdu->pdu_bhs;
	req.data.receive.data_segment_len =
	    conn->conn_max_recv_data_segment_length;
	req.data.receive.data_segment = pdu->pdu_data;

	if (ioctl(ctl_fd, CTL_ISCSI, &req) == -1) {
		log_err(1, "error issuing CTL_ISCSI ioctl; "
		    "dropping connection");
	}

	if (req.status != CTL_ISCSI_OK) {
		log_errx(1, "error returned from CTL iSCSI receive: "
		    "%s; dropping connection", req.error_str);
	}

}

#endif /* ICL_KERNEL_PROXY */

/*
 * XXX: I CANT INTO LATIN
 */
void
kernel_capsicate(void)
{
	cap_rights_t rights;
	const unsigned long cmds[] = { CTL_ISCSI };

	cap_rights_init(&rights, CAP_IOCTL);
	if (caph_rights_limit(ctl_fd, &rights) < 0)
		log_err(1, "cap_rights_limit");

	if (caph_ioctls_limit(ctl_fd, cmds, nitems(cmds)) < 0)
		log_err(1, "cap_ioctls_limit");

	if (caph_enter() < 0)
		log_err(1, "cap_enter");

	if (cap_sandboxed())
		log_debugx("Capsicum capability mode enabled");
	else
		log_warnx("Capsicum capability mode not supported");
}

