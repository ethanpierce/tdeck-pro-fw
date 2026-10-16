# Current radio protocol

The active radio service now uses the bounded Meshtastic channel protocol
documented in [MESH.md](MESH.md). It implements channel text broadcasts and node
information as a non-relaying leaf client. Mesh defaults off with the region
unset, and no transmission occurs until the user explicitly configures and
enables the US profile for the 915 MHz board.

On air, a little-endian 16-byte Meshtastic header contains destination, sender,
packet ID, flags/hops, channel hash, next-hop and relay bytes. The body is a
protobuf Data message encrypted using channel AES-128/256-CTR. Supported
applications are TEXT_MESSAGE_APP (1) and NODEINFO_APP (4, User protobuf). The
nonce is packet-ID as 64-bit little endian, sender as 32-bit little endian,
then four zero bytes. The radio uses sync word `0x2b`, a 16-symbol preamble and the
selected Meshtastic preset. Packets are at most 255 bytes.

The exact upstream reference is Meshtastic firmware
[54e0d8d0ab2ff56b3a9ce967e53f79e49af560fb](https://github.com/meshtastic/firmware/tree/54e0d8d0ab2ff56b3a9ce967e53f79e49af560fb)
and protobufs
[6b1ded439633cd03d4af85b44231b91d1d106278](https://github.com/meshtastic/protobufs/tree/6b1ded439633cd03d4af85b44231b91d1d106278).
See MESH.md for settings, limits, encryption caveats and validation. Physical
peer interoperability remains unverified.

The format below is retained as historical documentation and for old codec
fixtures. The current service no longer transmits or receives DK packets. It
cannot communicate with that old plaintext protocol.

# Legacy direct LoRa protocol, version 1

Older Handheld OS builds used custom single-hop, plaintext broadcast packets. Both peers must use this packet format and the same physical-layer settings. Meshtastic and LoRaWAN peers will not understand it.

## Default radio settings

| Setting | Value |
| --- | --- |
| Radio | SX1262 |
| Center frequency | 915.0 MHz |
| Bandwidth | 125 kHz |
| Spreading factor | 7 |
| Coding rate | 4/5 (`RadioLib` argument `5`) |
| Sync word | `0x12` |
| Output power | 10 dBm |
| Preamble | 8 symbols |
| Packet CRC | 2 bytes, enabled |
| TCXO supply setting | 2.4 V |
| RF switch | DIO2 control enabled |

The physical-layer CRC detects corruption. It does not authenticate a sender or encrypt the packet.

## Packet bytes

Multi-byte integers are unsigned, most-significant byte first. There is no terminating NUL.

| Byte offset | Size | Meaning |
| --- | --- | --- |
| 0 | 2 | ASCII `DK` (`0x44 0x4B`) |
| 2 | 1 | Version, `1` |
| 3 | 1 | Reserved/flags, must be `0` |
| 4 | 4 | Nonzero sender node ID |
| 8 | 4 | Sender sequence number |
| 12 | 1 | Text length, `1`–`160` |
| 13 | text length | Printable ASCII text bytes, `0x20`–`0x7E` |

Total payload length is 14–173 bytes and must equal `13 + text length`. Decoders reject wrong magic/version/flags, zero sender ID, an inconsistent length, nonprintable payload bytes or a packet outside the size bounds.

For sender `0x01020304`, sequence `1` and text `Hi`:

```text
44 4B 01 00 01 02 03 04 00 00 00 01 02 48 69
```

The ESP32 derives the sender ID from its eFuse MAC and starts its transmit sequence from a random value at each boot. This is a convenient local identifier, not a cryptographic identity or guaranteed collision-free address. The application ignores its own sender ID and filters duplicates by sender/sequence within retained history.

## Behavior and limits

The sender transmits once after an explicit user send and returns to receive. There is no destination address, acknowledgement, retry, routing, fragmentation, key exchange or encryption. A transmit timeout yields an unknown result. All matching receivers in range can read the text, and a sender ID can be forged.

Use short, infrequent messages for initial tests with antennas fitted. “Transmitted” confirms only that the local radio completed the operation. Observe a second peer to establish reception. A production protocol needs decisions about channel access, regional settings, security, replay protection and delivery behavior before extending this format.
