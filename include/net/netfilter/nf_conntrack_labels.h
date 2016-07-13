#include <linux/types.h>
#include <net/net_namespace.h>
#include <linux/netfilter/nf_conntrack_common.h>
#include <linux/netfilter/nf_conntrack_tuple_common.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_extend.h>

#include <uapi/linux/netfilter/xt_connlabel.h>

#define NF_CT_LABELS_MAX_SIZE ((XT_CONNLABEL_MAXBIT + 1) / BITS_PER_BYTE)

struct nf_conn_labels {
	u8 words;
	unsigned long bits[];
};

static inline struct nf_conn_labels *nf_ct_labels_find(const struct nf_conn *ct)
{
#ifdef CONFIG_NF_CONNTRACK_LABELS
	return nf_ct_ext_find(ct, NF_CT_EXT_LABELS);
#else
	return NULL;
#endif
}

static inline struct nf_conn_labels *nf_ct_labels_ext_add(struct nf_conn *ct)
{
#ifdef CONFIG_NF_CONNTRACK_LABELS
	struct nf_conn_labels *cl_ext;
	struct net *net = nf_ct_net(ct);
	u8 words;

	words = ACCESS_ONCE(net->ct.label_words);
	if (words == 0)
		return NULL;

	cl_ext = nf_ct_ext_add_length(ct, NF_CT_EXT_LABELS,
				      words * sizeof(long), GFP_ATOMIC);
	if (cl_ext != NULL)
		cl_ext->words = words;

	return cl_ext;
#else
	return NULL;
#endif
}

int nf_connlabel_set(struct nf_conn *ct, u16 bit);

int nf_connlabels_replace(struct nf_conn *ct,
			  const u32 *data, const u32 *mask, unsigned int words);

#ifdef CONFIG_NF_CONNTRACK_LABELS
int nf_conntrack_labels_init(void);
void nf_conntrack_labels_fini(void);
int nf_connlabels_get(struct net *net, unsigned int bit);
void nf_connlabels_put(struct net *net);
#else
static inline int nf_conntrack_labels_init(void) { return 0; }
static inline void nf_conntrack_labels_fini(void) {}
static inline int nf_connlabels_get(struct net *net, unsigned int bit) { return 0; }
static inline void nf_connlabels_put(struct net *net) {}
#endif
