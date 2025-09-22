/*	$OpenBSD: nlm_prot.x,v 1.8 2008/06/15 04:41:39 sturm Exp $	*/

#ifdef RPC_HDR
%#define LM_MAXSTRLEN	1024
%#define MAXNAMELEN	LM_MAXSTRLEN+1
#endif

enum nlm_stats {
	nlm_granted = 0,
	nlm_denied = 1,
	nlm_denied_nolocks = 2,
	nlm_blocked = 3,
	nlm_denied_grace_period = 4
};

struct nlm_holder {
	bool exclusive;
	int svid;
	netobj oh;
	unsigned l_offset;
	unsigned l_len;
};

union nlm_testrply switch (nlm_stats stat) {
	case nlm_denied:
		struct nlm_holder holder;
	default:
		void;
};

struct nlm_stat {
	nlm_stats stat;
};

struct nlm_res {
	netobj cookie;
	nlm_stat stat;
};

struct nlm_testres {
	netobj cookie;
	nlm_testrply stat;
};

struct nlm_lock {
	string caller_name<LM_MAXSTRLEN>;
	netobj fh;
	netobj oh;
	int svid;
	unsigned l_offset;
	unsigned l_len;
};

struct nlm_lockargs {
	netobj cookie;
	bool block;
	bool exclusive;
	struct nlm_lock alock;
	bool reclaim;
	int state;
};

struct nlm_cancargs {
	netobj cookie;
	bool block;
	bool exclusive;
	struct nlm_lock alock;
};

struct nlm_testargs {
	netobj cookie;
	bool exclusive;
	struct nlm_lock alock;
};

struct nlm_unlockargs {
	netobj cookie;
	struct nlm_lock alock;
};


#ifdef RPC_HDR
#endif
enum	fsh_mode {
	fsm_DN  = 0,
	fsm_DR  = 1,
	fsm_DW  = 2,
	fsm_DRW = 3
};

enum	fsh_access {
	fsa_NONE = 0,
	fsa_R    = 1,
	fsa_W    = 2,
	fsa_RW   = 3
};

struct	nlm_share {
	string caller_name<LM_MAXSTRLEN>;
	netobj	fh;
	netobj	oh;
	fsh_mode	mode;
	fsh_access	access;
};

struct	nlm_shareargs {
	netobj	cookie;
	nlm_share	share;
	bool	reclaim;
};

struct	nlm_shareres {
	netobj	cookie;
	nlm_stats	stat;
	int	sequence;
};

struct	nlm_notify {
	string name<MAXNAMELEN>;
	int state;
};

enum nlm4_stats {
	nlm4_granted			= 0,
	nlm4_denied			= 1,
	nlm4_denied_nolock		= 2,
	nlm4_blocked			= 3,
	nlm4_denied_grace_period	= 4,
	nlm4_deadlck			= 5,
	nlm4_rofs			= 6,
	nlm4_stale_fh			= 7,
	nlm4_fbig			= 8,
	nlm4_failed			= 9
};

struct nlm4_stat {
	nlm4_stats stat;
};

struct nlm4_holder {
	bool exclusive;
	u_int32_t svid;
	netobj oh;
	u_int64_t l_offset;
	u_int64_t l_len;
};

struct nlm4_lock {
	string caller_name<MAXNAMELEN>;
	netobj fh;
	netobj oh;
	u_int32_t svid;
	u_int64_t l_offset;
	u_int64_t l_len;
};

struct nlm4_share {
	string caller_name<MAXNAMELEN>;
	netobj fh;
	netobj oh;
	fsh_mode mode;
	fsh_access access;
};

union nlm4_testrply switch (nlm4_stats stat) {
	case nlm_denied:
		struct nlm4_holder holder;
	default:
		void;
};

struct nlm4_testres {
	netobj cookie;
	nlm4_testrply stat;
};

struct nlm4_testargs {
	netobj cookie;
	bool exclusive;
	struct nlm4_lock alock;
};

struct nlm4_res {
	netobj cookie;
	nlm4_stat stat;
};

struct nlm4_lockargs {
	netobj cookie;
	bool block;
	bool exclusive;
	struct nlm4_lock alock;
	bool reclaim;
	int state;
};

struct nlm4_cancargs {
	netobj cookie;
	bool block;
	bool exclusive;
	struct nlm4_lock alock;
};

struct nlm4_unlockargs {
	netobj cookie;
	struct nlm4_lock alock;
};

struct	nlm4_shareargs {
	netobj	cookie;
	nlm4_share	share;
	bool	reclaim;
};

struct	nlm4_shareres {
	netobj	cookie;
	nlm4_stats	stat;
	int	sequence;
};

struct nlm_sm_status {
	string mon_name<LM_MAXSTRLEN>;
	int state;
	opaque priv[16];
};

program NLM_PROG {
	version NLM_SM {
		void NLM_SM_NOTIFY(struct nlm_sm_status) = 1;
	} = 0;

	version NLM_VERS {

		nlm_testres	NLM_TEST(struct nlm_testargs) =	1;

		nlm_res		NLM_LOCK(struct nlm_lockargs) =	2;

		nlm_res		NLM_CANCEL(struct nlm_cancargs) = 3;

		nlm_res		NLM_UNLOCK(struct nlm_unlockargs) = 4;

		nlm_res		NLM_GRANTED(struct nlm_testargs)= 5;

		void		NLM_TEST_MSG(struct nlm_testargs) = 6;
		void		NLM_LOCK_MSG(struct nlm_lockargs) = 7;
		void		NLM_CANCEL_MSG(struct nlm_cancargs) = 8;
		void		NLM_UNLOCK_MSG(struct nlm_unlockargs) = 9;
		void		NLM_GRANTED_MSG(struct nlm_testargs) = 10;
		void		NLM_TEST_RES(nlm_testres) = 11;
		void		NLM_LOCK_RES(nlm_res) = 12;
		void		NLM_CANCEL_RES(nlm_res) = 13;
		void		NLM_UNLOCK_RES(nlm_res) = 14;
		void		NLM_GRANTED_RES(nlm_res) = 15;
	} = 1;

	version NLM_VERSX {
		nlm_shareres	NLM_SHARE(nlm_shareargs) = 20;
		nlm_shareres	NLM_UNSHARE(nlm_shareargs) = 21;
		nlm_res		NLM_NM_LOCK(nlm_lockargs) = 22;
		void		NLM_FREE_ALL(nlm_notify) = 23;
	} = 3;

	version NLM_VERS4 {
		nlm4_testres NLM4_TEST(nlm4_testargs) = 1;
		nlm4_res NLM4_LOCK(nlm4_lockargs) = 2;
		nlm4_res NLM4_CANCEL(nlm4_cancargs) = 3;
		nlm4_res NLM4_UNLOCK(nlm4_unlockargs) = 4;
		nlm4_res NLM4_GRANTED(nlm4_testargs) = 5;
		void NLM4_TEST_MSG(nlm4_testargs) = 6;
		void NLM4_LOCK_MSG(nlm4_lockargs) = 7;
		void NLM4_CANCEL_MSG(nlm4_cancargs) = 8;
		void NLM4_UNLOCK_MSG(nlm4_unlockargs) = 9;
		void NLM4_GRANTED_MSG(nlm4_testargs) = 10;
		void NLM4_TEST_RES(nlm4_testres) = 11;
		void NLM4_LOCK_RES(nlm4_res) = 12;
		void NLM4_CANCEL_RES(nlm4_res) = 13;
		void NLM4_UNLOCK_RES(nlm4_res) = 14;
		void NLM4_GRANTED_RES(nlm4_res) = 15;
		nlm4_shareres NLM4_SHARE(nlm4_shareargs) = 20;
		nlm4_shareres NLM4_UNSHARE(nlm4_shareargs) = 21;
		nlm4_res NLM4_NM_LOCK(nlm4_lockargs) = 22;
		void NLM4_FREE_ALL(nlm_notify) = 23;
	} = 4;
} = 100021;

