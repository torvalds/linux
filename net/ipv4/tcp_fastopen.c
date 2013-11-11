#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/tcp.h>
#include <linux/rcupdate.h>
#include <linux/rculist.h>
#include <net/inetpeer.h>
#include <net/tcp.h>

int sysctl_tcp_fastopen __read_mostly;

struct tcp_fastopen_context __rcu *tcp_fastopen_ctx;

static DEFINE_SPINLOCK(tcp_fastopen_ctx_lock);

static void tcp_fastopen_ctx_free(struct rcu_head *head)
{
	struct tcp_fastopen_context *ctx =
	    container_of(head, struct tcp_fastopen_context, rcu);
	crypto_free_cipher(ctx->tfm);
	kfree(ctx);
}

int tcp_fastopen_reset_cipher(void *key, unsigned int len)
{
	int err;
	struct tcp_fastopen_context *ctx, *octx;

	ctx = kmalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	ctx->tfm = crypto_alloc_cipher("aes", 0, 0);

	if (IS_ERR(ctx->tfm)) {
		err = PTR_ERR(ctx->tfm);
error:		kfree(ctx);
		pr_err("TCP: TFO aes cipher alloc error: %d\n", err);
		return err;
	}
	err = crypto_cipher_setkey(ctx->tfm, key, len);
	if (err) {
		pr_err("TCP: TFO cipher key error: %d\n", err);
		crypto_free_cipher(ctx->tfm);
		goto error;
	}
	memcpy(ctx->key, key, len);

	spin_lock(&tcp_fastopen_ctx_lock);

	octx = rcu_dereference_protected(tcp_fastopen_ctx,
				lockdep_is_held(&tcp_fastopen_ctx_lock));
	rcu_assign_pointer(tcp_fastopen_ctx, ctx);
	spin_unlock(&tcp_fastopen_ctx_lock);

	if (octx)
		call_rcu(&octx->rcu, tcp_fastopen_ctx_free);
	return err;
}

/* Computes the fastopen cookie for the peer.
 * The peer address is a 128 bits long (pad with zeros for IPv4).
 *
 * The caller must check foc->len to determine if a valid cookie
 * has been generated successfully.
*/
void tcp_fastopen_cookie_gen(__be32 addr, struct tcp_fastopen_cookie *foc)
{
	__be32 peer_addr[4] = { addr, 0, 0, 0 };
	struct tcp_fastopen_context *ctx;

	rcu_read_lock();
	ctx = rcu_dereference(tcp_fastopen_ctx);
	if (ctx) {
		crypto_cipher_encrypt_one(ctx->tfm,
					  foc->val,
					  (__u8 *)peer_addr);
		foc->len = TCP_FASTOPEN_COOKIE_SIZE;
	}
	rcu_read_unlock();
}

static int __init tcp_fastopen_init(void)
{
	__u8 key[TCP_FASTOPEN_KEY_LENGTH];

	get_random_bytes(key, sizeof(key));
	tcp_fastopen_reset_cipher(key, sizeof(key));
	return 0;
}

late_initcall(tcp_fastopen_init);
