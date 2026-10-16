// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <mesh_protocol.h>
// Meshtastic channel AES-CTR. This wire format has no authentication tag;
// successful decoding is not proof of a sender's identity.
bool meshCrypt(const deck::MeshOptions& options,uint32_t from,uint32_t id,uint8_t* bytes,size_t size);
bool meshCryptoSelfTest();
