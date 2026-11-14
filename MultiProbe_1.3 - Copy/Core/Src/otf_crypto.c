/* otf_crypto.c - OTFDEC-compatible AES-CTR keystream + per-device key derivation (portable, no libc). */
#include "otf_crypto.h"

#define OTF_FN __attribute__((optimize("O2,no-tree-loop-distribute-patterns")))

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

OTF_FN void OTF_Derive(const uint32_t uid[3], const uint8_t secret[16], uint8_t key[16], uint32_t nonce[2])
{
    AES128_Ctx c;
    uint8_t blk[16], out[16];

    AES128_Init(&c, secret);

    /* key = AES_secret( UID || "OTFK" ) */
    put_be32(&blk[0], uid[0]); put_be32(&blk[4], uid[1]); put_be32(&blk[8], uid[2]);
    blk[12] = 'O'; blk[13] = 'T'; blk[14] = 'F'; blk[15] = 'K';
    AES128_EncryptBlock(&c, blk, key);

    /* nonce = first 8 bytes of AES_secret( UID || "OTFN" ) */
    blk[15] = 'N';
    AES128_EncryptBlock(&c, blk, out);
    nonce[1] = ((uint32_t)out[0] << 24) | ((uint32_t)out[1] << 16) | ((uint32_t)out[2] << 8) | out[3];
    nonce[0] = ((uint32_t)out[4] << 24) | ((uint32_t)out[5] << 16) | ((uint32_t)out[6] << 8) | out[7];
}

/*
 * Counter block, MSB first (RM0455 OTFDEC / AN5281 table 5):
 *   [127:96] NONCER1   [95:64] NONCER0   [63:48] 0   [47:32] REGx_VERSION
 *   [31:30] 0          [29:28] region id [27:0] address[31:4]
 * OTFDEC works little-endian on memory while AES is big-endian (AN5281 2.3), which for one 128-bit block is a
 * plain reversal of the 16 bytes -> ks_mem[i] = ks_aes[15 - i].
 */
OTF_FN void OTF_Keystream(const AES128_Ctx *ctx, const uint32_t nonce[2], uint32_t version,
                          uint32_t region, uint32_t addr, uint8_t ks[16])
{
    uint8_t iv[16], k[16];

    put_be32(&iv[0], nonce[1]);
    put_be32(&iv[4], nonce[0]);
    iv[8] = 0U; iv[9] = 0U;
    iv[10] = (uint8_t)(version >> 8);
    iv[11] = (uint8_t)version;
    put_be32(&iv[12], ((region & 3UL) << 28) | ((addr >> 4) & 0x0FFFFFFFUL));

    AES128_EncryptBlock(ctx, iv, k);
    for (uint32_t i = 0; i < 16U; i++) ks[i] = k[15U - i];
}
