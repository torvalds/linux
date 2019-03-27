/* $FreeBSD$ */

#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

const char     *const pccardd_file = "/var/tmp/.pccardd";
const char     *prog = "pccardq";
const char     *tmp_dir = "/tmp";
unsigned        slot_map = ~0;

void
usage(void)
{
    fprintf(stderr, "usage: %s [-a] [-n] [-s slot]\n", prog);
}

int
proc_arg(int ac, char **av)
{
    int             rc = -1;
    int             ch;

    char           *p = strrchr(av[0], '/');
    prog = p ? p + 1 : av[0];

    tmp_dir = getenv("TMPDIR") ? getenv("TMPDIR") : tmp_dir;

    while ((ch = getopt(ac, av, "ans:")) != -1) {
	switch (ch) {
	case 'a':
	    slot_map = ~0;
	    break;
	case 'n':
	    slot_map = 0;
	    break;
	case 's':
	    {
		int             n = atoi(optarg);
		if (n < 0 || n >= CHAR_BIT * sizeof slot_map) {
		    warnc(0, "Invalid slot number.");
		    usage();
		    goto out;
		}
		if (slot_map == ~0)
		    slot_map = 0;
		slot_map |= 1 << n;
	    }
	    break;
	default:
	    usage();
	    goto out;
	}
    }

    rc = 0;
  out:
    return rc;
}

int
connect_to_pccardd(char **path)
{
    int             so = -1;
    int             pccardd_len;
    struct sockaddr_un pccardq;
    struct sockaddr_un pccardd;

    if ((so = socket(PF_UNIX, SOCK_DGRAM, 0)) < 0) {
	warn("socket");
	goto err;
    }

    snprintf(pccardq.sun_path, sizeof pccardq.sun_path,
	     "%s/%s%ld%ld", tmp_dir, prog, (long) getpid(), (long) time(0));
    pccardq.sun_family = AF_UNIX;
    pccardq.sun_len = offsetof(struct sockaddr_un, sun_path) + strlen(pccardq.sun_path);
    if (bind(so, (struct sockaddr *) &pccardq, pccardq.sun_len) < 0) {
	warn("bind: %s", pccardq.sun_path);
	goto err;
    }
    if ((*path = strdup(pccardq.sun_path)) == NULL) {
	warn("strdup");
	goto err;
    }

    pccardd_len = strlen(pccardd_file) + 1;
    if (pccardd_len > sizeof pccardd.sun_path) {
	warnc(0, "%s: too long", pccardd_file);
	goto err;
    }
    pccardd.sun_len = offsetof(struct sockaddr_un, sun_path) + pccardd_len;
    pccardd.sun_family = AF_UNIX;
    strcpy(pccardd.sun_path, pccardd_file);
    if (connect(so, (struct sockaddr *) &pccardd, pccardd.sun_len) < 0) {
	warn("connect: %s", pccardd_file);
	goto err;
    }
    return so;
  err:
    if (so >= 0)
	close(so);
    return -1;
}

int
get_slot_number(int so)
{
    char            buf[8];
    int             rv;
    int             nslot;

    if ((rv = write(so, "S", 1)) < 1) {
	warn("write");
	goto err;
    } else if (rv != 1) {
	warnc(0, "write: fail.");
	goto err;
    }

    if ((rv = read(so, buf, sizeof buf)) < 0) {
	warn("read");
	goto err;
    }
    buf[sizeof buf - 1] = 0;
    if (sscanf(buf, "%d", &nslot) != 1) {
	warnc(0, "Invalid response.");
	goto err;
    }
    return nslot;
  err:
    return -1;
}

enum {
    SLOT_EMPTY = 0,
    SLOT_FILLED = 1,
    SLOT_INACTIVE = 2,
    SLOT_UNDEFINED = 9
};

int
get_slot_info(int so, int slot, char **manuf, char **version, char
	      **device, int *state)
{
    int             rc = -1;
    int             rv;
    static char     buf[1024];
    int             slen;
    char           *s;
    char           *sl;

    char           *_manuf;
    char           *_version;
    char           *_device;

    if ((slen = snprintf(buf, sizeof buf, "N%d", slot)) < 0) {
	warnc(0, "write");
	goto err;
    }

    if ((rv = write(so, buf, slen)) < 0) {
	warn("write");
	goto err;
    } else if (rv != slen) {
	warnc(0, "write");
	goto err;
    }

    if ((rv = read(so, buf, sizeof buf)) < 0) {
	warn("read");
	goto err;
    }

    s = buf;
    if ((sl = strsep(&s, "~")) == NULL)
	goto parse_err;
    if (atoi(sl) != slot)
	goto parse_err;
    if ((_manuf = strsep(&s, "~")) == NULL)
	goto parse_err;
    if ((_version = strsep(&s, "~")) == NULL)
	goto parse_err;
    if ((_device = strsep(&s, "~")) == NULL)
	goto parse_err;
    if (sscanf(s, "%1d", state) != 1)
	goto parse_err;
    if (s != NULL && strchr(s, '~') != NULL)
	goto parse_err;

    if ((*manuf = strdup(_manuf)) == NULL) {
	warn("strdup");
	goto err;
    }
    if ((*version = strdup(_version)) == NULL) {
	warn("strdup");
	goto err;
    }
    if ((*device = strdup(_device)) == NULL) {
	warn("strdup");
	goto err;
    }
    if (*manuf == NULL || *version == NULL || *device == NULL) {
	warn("strdup");
	goto err;
    }

    rc = 0;
  err:
    return rc;
  parse_err:
    warnc(0, "Invalid response: %*s", rv, buf);
    return rc;
}

const char *
strstate(int state)
{
    switch (state) {
    case 0:
	return "empty";
    case 1:
	return "filled";
    case 2:
	return "inactive";
    default:
	return "unknown";
    }
}

int
main(int ac, char **av)
{
    char           *path = NULL;
    int             so = -1;
    int             nslot;
    int             i;

    if (proc_arg(ac, av) < 0)
	goto out;
    if ((so = connect_to_pccardd(&path)) < 0)
	goto out;
    if ((nslot = get_slot_number(so)) < 0)
	goto out;
    if (slot_map == 0) {
	printf("%d\n", nslot);
    } else {
	for (i = 0; i < nslot; i++) {
	    if ((slot_map & (1 << i))) {
		char           *manuf;
		char           *version;
		char           *device;
		int             state;

		if (get_slot_info(so, i, &manuf, &version, &device,
				  &state) < 0)
		    goto out;
		if (manuf == NULL || version == NULL || device == NULL)
		    goto out;
		printf("%d~%s~%s~%s~%s\n",
		       i, manuf, version, device, strstate(state));
		free(manuf);
		free(version);
		free(device);
	    }
	}
    }
  out:
    if (path) {
	unlink(path);
	free(path);
    }
    if (so >= 0)
	close(so);
    exit(0);
}
