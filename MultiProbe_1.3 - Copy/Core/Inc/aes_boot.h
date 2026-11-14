#ifndef AES_BOOT_H
#define AES_BOOT_H
#include <stdint.h>

typedef struct { uint8_t rk[176]; } AES128_Ctx;

void AES128_Init(AES128_Ctx *c, const uint8_t key[16]);
void AES128_EncryptBlock(const AES128_Ctx *c, const uint8_t in[16], uint8_t out[16]);

#endif
