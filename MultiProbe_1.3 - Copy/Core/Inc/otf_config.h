#ifndef OTF_CONFIG_H
#define OTF_CONFIG_H
/*
 * OTFDEC configuration (code confidentiality).
 *
 *  OTF_ENABLE   1 : firmware image in the external flash is stored AES-128-CTR encrypted and decrypted on the fly by
 *                   OTFDEC1 (region 1, MODE = all reads). The key + nonce are derived per chip from the 96-bit UID
 *                   and OTF_SECRET, so an image copied from one board's flash is useless on any other board.
 *               0 : plain XIP (debug aid: use it to separate "QSPI/XIP problem" from "crypto problem").
 *
 *  OTF_SECRET   16 secret bytes. CHANGE IT for production (and keep it out of source control!), and never change it
 *               again afterwards: boards provisioned with an old secret would no longer decrypt.
 *               It lives in INTERNAL flash, so set Read-Out Protection (RDP level 1, or 2 for final production) on
 *               the board - otherwise anybody can read the secret over SWD.
 */
#define OTF_ENABLE        1
#define OTF_FW_VERSION    1U      /* 16-bit REGx_VERSION field; part of the AES counter */

#define OTF_SECRET_BYTES  { 0xCD, 0x50, 0x4E, 0x64, 0xE9, 0x2D, 0x31, 0x2F, 0x70, 0xF9, 0xDB, 0x56, 0x04, 0xC3, 0xAC, 0x86 }

#endif
