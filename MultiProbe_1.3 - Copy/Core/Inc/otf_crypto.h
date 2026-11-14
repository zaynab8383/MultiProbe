#ifndef OTF_CRYPTO_H
#define OTF_CRYPTO_H
#include <stdint.h>
#include "aes_boot.h"

/* key[16]: AES key bytes (key[0..3] = OTFDEC_RxKEYR3, ... key[12..15] = RxKEYR0, each big-endian)
 * nonce[2]: nonce[0] = RxNONCER0, nonce[1] = RxNONCER1                                           */
void OTF_Derive(const uint32_t uid[3], const uint8_t secret[16], uint8_t key[16], uint32_t nonce[2]);

/* Keystream for the 16-byte block at AHB address 'addr' (multiple of 16), already in MEMORY byte order,
 * i.e. ciphertext_byte[i] = plaintext_byte[i] ^ ks[i]. region = 0..3 (OTFDEC region number - 1). */
void OTF_Keystream(const AES128_Ctx *ctx, const uint32_t nonce[2], uint32_t version,
                   uint32_t region, uint32_t addr, uint8_t ks[16]);
#endif
