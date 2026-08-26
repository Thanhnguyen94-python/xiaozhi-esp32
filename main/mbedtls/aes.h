#ifndef MBEDTLS_AES_H
#define MBEDTLS_AES_H

#include <aes/esp_aes.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef esp_aes_context mbedtls_aes_context;

#ifndef MBEDTLS_AES_ENCRYPT
#define MBEDTLS_AES_ENCRYPT 1
#endif

#ifndef MBEDTLS_AES_DECRYPT
#define MBEDTLS_AES_DECRYPT 0
#endif

static inline void mbedtls_aes_init(mbedtls_aes_context *ctx) {
    esp_aes_init(ctx);
}

static inline void mbedtls_aes_free(mbedtls_aes_context *ctx) {
    esp_aes_free(ctx);
}

static inline int mbedtls_aes_setkey_enc(mbedtls_aes_context *ctx, const unsigned char *key, unsigned int keybits) {
    return esp_aes_setkey(ctx, key, keybits);
}

static inline int mbedtls_aes_setkey_dec(mbedtls_aes_context *ctx, const unsigned char *key, unsigned int keybits) {
    return esp_aes_setkey(ctx, key, keybits);
}

static inline int mbedtls_aes_crypt_ctr(mbedtls_aes_context *ctx,
                                        size_t length,
                                        size_t *nc_off,
                                        unsigned char nonce_counter[16],
                                        unsigned char stream_block[16],
                                        const unsigned char *input,
                                        unsigned char *output) {
    return esp_aes_crypt_ctr(ctx, length, nc_off, nonce_counter, stream_block, input, output);
}

static inline int mbedtls_aes_crypt_cfb128(mbedtls_aes_context *ctx,
                                           int mode,
                                           size_t length,
                                           size_t *iv_off,
                                           unsigned char iv[16],
                                           const unsigned char *input,
                                           unsigned char *output) {
    return esp_aes_crypt_cfb128(ctx, mode, length, iv_off, iv, input, output);
}

#ifdef __cplusplus
}
#endif

#endif
