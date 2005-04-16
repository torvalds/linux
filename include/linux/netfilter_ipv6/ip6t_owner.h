#ifndef _IP6T_OWNER_H
#define _IP6T_OWNER_H

/* match and invert flags */
#define IP6T_OWNER_UID	0x01
#define IP6T_OWNER_GID	0x02
#define IP6T_OWNER_PID	0x04
#define IP6T_OWNER_SID	0x08

struct ip6t_owner_info {
    uid_t uid;
    gid_t gid;
    pid_t pid;
    pid_t sid;
    u_int8_t match, invert;	/* flags */
};

#endif /*_IPT_OWNER_H*/
