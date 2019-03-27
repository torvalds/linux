/*	$KAME: rijndael.c,v 1.3 2003/08/28 14:20:22 itojun Exp $	*/

/*
 * rijndael-alg-fst.c
 *
 * @version 3.0 (December 2000)
 *
 * Optimised ANSI C code for the Rijndael cipher (now AES)
 *
 * @author Vincent Rijmen <vincent.rijmen@esat.kuleuven.ac.be>
 * @author Antoon Bosselaers <antoon.bosselaers@esat.kuleuven.ac.be>
 * @author Paulo Barreto <paulo.barreto@terra.com.br>
 *
 * This code is hereby placed in the public domain.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ''AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#ifdef _KERNEL
#include <sys/systm.h>
#endif

#include <crypto/rijndael/rijndael.h>

void
rijndael_set_key(rijndael_ctx *ctx, const u_char *key, int bits)
{

	ctx->Nr = rijndaelKeySetupEnc(ctx->ek, key, bits);
	rijndaelKeySetupDec(ctx->dk, key, bits);
}

void
rijndael_decrypt(const rijndael_ctx *ctx, const u_char *src, u_char *dst)
{

	rijndaelDecrypt(ctx->dk, ctx->Nr, src, dst);
}

void
rijndael_encrypt(const rijndael_ctx *ctx, const u_char *src, u_char *dst)
{

	rijndaelEncrypt(ctx->ek, ctx->Nr, src, dst);
}
