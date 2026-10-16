# Handheld OS direct LoRa protocol, version 1

This starter uses custom single-hop, plaintext broadcast packets. Both peers must use this packet format and the same physical-layer settings. Meshtastic and LoRaWAN peers will not understand it.

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
