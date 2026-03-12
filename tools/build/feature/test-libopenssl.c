#include <openssl/ssl.h>
#include <openssl/opensslv.h>

int main(void)
{
	return SSL_library_init();
}
