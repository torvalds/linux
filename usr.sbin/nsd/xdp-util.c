/*
 * xdp-util.h -- set of xdp related helpers
 *
 * Copyright (c) 2024, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#include "config.h"

#ifdef USE_XDP

#include <errno.h>
#include <linux/ethtool.h>
#include <linux/if.h>
#include <linux/sockios.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* for the capability modifications */
/* inspired by https://stackoverflow.com/questions/13183327/drop-root-uid-while-retaining-cap-sys-nice#13186076 */
#include <sys/capability.h>

#include "xdp-util.h"

int ethtool_channels_get(char const *ifname) {
	struct ethtool_channels channels;
	struct ifreq ifr;
	int fd, rc;
	uint32_t queue_count = 0;

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		return -1;
	}

	channels.cmd = ETHTOOL_GCHANNELS;
	ifr.ifr_data = (void *)&channels;
	strlcpy(ifr.ifr_name, ifname, IFNAMSIZ);
	rc = ioctl(fd, SIOCETHTOOL, &ifr);
	if (rc != 0) {
		if (errno != EOPNOTSUPP) {
			close(fd);
			return -errno;
		}
	}

	if (errno == EOPNOTSUPP) {
		queue_count = 1;
	} else {
		/* ethtool_channels offers
		 * max_{rx,tx,other,combined} and
		 * {rx,tx,other,combined}_count
		 *
		 * Maybe check for different variations of rx/tx and combined
         * queues in the future? */
		if (channels.combined_count > 0) {
			queue_count = channels.combined_count;
		} else if (channels.rx_count > 0) {
			queue_count = channels.rx_count;
		} else {
			queue_count = 1;
		}
	}

	close(fd);
	return (int) queue_count;
}

/* CAPABILITY SHENANIGANS */

void set_caps(int unset_setid_caps) {
#define NUM_CAPS_ALL 4
#define NUM_CAPS_NEEDED 2
	cap_t caps;
	const cap_value_t cap_list[NUM_CAPS_ALL] = {
		CAP_BPF,
		/* CAP_SYS_ADMIN, [> SYS_ADMIN needed for xdp_multiprog__get_from_ifindex <] */
		/* CAP_SYS_RESOURCE, */
		CAP_NET_ADMIN,
		/* CAP_NET_RAW, */
		CAP_SETUID,
		CAP_SETGID
	};

	if (!(caps = cap_init()))
		goto out;

	/* set all above capabilities in permitted and effective sets */
	if (cap_set_flag(caps, CAP_EFFECTIVE, NUM_CAPS_ALL, cap_list, CAP_SET))
		goto out;
	if (cap_set_flag(caps, CAP_PERMITTED, NUM_CAPS_ALL, cap_list, CAP_SET))
		goto out;

	/* unset unneeded capabilities */
	if (unset_setid_caps) {
		if (cap_set_flag(caps, CAP_EFFECTIVE, NUM_CAPS_NEEDED, cap_list + NUM_CAPS_NEEDED, CAP_CLEAR))
			goto out;
		if (cap_set_flag(caps, CAP_PERMITTED, NUM_CAPS_NEEDED, cap_list + NUM_CAPS_NEEDED, CAP_CLEAR))
			goto out;
	}

	/* set capabilities as configured above to current process */
	if (cap_set_proc(caps))
		goto out;

out:
	cap_free(caps);
}

#endif /* USE_XDP */
