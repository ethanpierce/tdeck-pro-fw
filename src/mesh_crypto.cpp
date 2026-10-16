// SPDX-License-Identifier: GPL-3.0-only
#include "mesh_crypto.h"
#include <mbedtls/aes.h>
#include <cstring>
bool meshCrypt(const deck::MeshOptions& options,uint32_t from,uint32_t id,uint8_t* bytes,size_t size) {
    if(!deck::validMeshOptions(options) || !from || !id || !bytes || !size || size>deck::MeshFrameLimit-deck::MeshHeaderSize)return false;
    uint8_t nonce[16],stream[16]{};size_t offset=0;deck::meshNonce(from,id,nonce);
    mbedtls_aes_context aes;mbedtls_aes_init(&aes);
    const int key=mbedtls_aes_setkey_enc(&aes,options.psk,unsigned(options.keyBytes)*8);
    const int result=key?key:mbedtls_aes_crypt_ctr(&aes,size,&offset,nonce,stream,bytes,bytes);
    mbedtls_aes_free(&aes);std::memset(nonce,0,sizeof(nonce));std::memset(stream,0,sizeof(stream));return result==0;
}
bool meshCryptoSelfTest() {
    // Independently produced with Python cryptography AES-CTR using the
    // pinned upstream nonce/key convention; verifies native SDK crypto before RF.
    const uint8_t plain[]={0x08,0x01,0x12,0x05,0x48,0x65,0x6c,0x6c,0x6f,0x48,0x00};
    const uint8_t expected128[]={0x19,0xe2,0x9e,0x59,0xd7,0xe8,0x35,0xae,0x44,0x4b,0x88};
    const uint8_t expected256[]={0xd2,0xb2,0x80,0xd8,0x2c,0x08,0xb4,0x19,0x27,0x85,0xa6};
    deck::MeshOptions options;uint8_t bytes[sizeof(plain)];std::memcpy(bytes,plain,sizeof(bytes));
    if(!meshCrypt(options,0x11223344,0x55667788,bytes,sizeof(bytes)) || std::memcmp(bytes,expected128,sizeof(bytes)))return false;
    if(!meshCrypt(options,0x11223344,0x55667788,bytes,sizeof(bytes)) || std::memcmp(bytes,plain,sizeof(bytes)))return false;
    options.keyBytes=32;for(unsigned n=0;n<32;++n)options.psk[n]=uint8_t(n);std::memcpy(bytes,plain,sizeof(bytes));
    if(!meshCrypt(options,0x11223344,0x55667788,bytes,sizeof(bytes)) || std::memcmp(bytes,expected256,sizeof(bytes)))return false;
    // A second block also checks counter byte ordering, not only the first IV.
    uint8_t blocks[32];for(unsigned n=0;n<sizeof(blocks);++n)blocks[n]=uint8_t(n);
    const uint8_t expectedBlocks[]={0xda,0xb2,0x90,0xde,0x60,0x68,0xde,0x72,0x40,0xc4,0xac,0xcc,0x68,0x1a,0xa0,0xa9,0x1f,0x76,0xd8,0xc1,0xa1,0xe7,0x71,0x82,0x4c,0x75,0x0a,0xd9,0xce,0xbd,0xa1,0x8b};
    return meshCrypt(options,0x11223344,0x55667788,blocks,sizeof(blocks)) && !std::memcmp(blocks,expectedBlocks,sizeof(blocks));
}
