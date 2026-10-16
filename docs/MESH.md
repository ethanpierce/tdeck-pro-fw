# Meshtastic channel messaging

Handheld OS includes a bounded, native **Meshtastic-compatible leaf client** for
channel text broadcasts. It uses the existing SX1262 and RadioLib portable HAL;
it does not import the Arduino firmware or require Arduino libraries.

Mesh starts **off**, with the region **Unset**. Select **US** for this 915 MHz
board, choose the same preset/channel/key as the other devices and explicitly
enable Mesh. Configuration alone sends nothing. Send a channel message from the
Mesh conversation, or use **Announce** in Heard nodes to broadcast node information.
There are no automatic boot announcements or position broadcasts. Antennas must
be fitted before transmission. This build exposes only the US profile; other
regions and hardware profiles are unsupported.

## Implemented subset

- Channel text broadcasts, with an explicit 160-byte outgoing limit and up to
  233 bytes of valid UTF-8 incoming text when the full packet fits the radio limit.
- NODEINFO/User announcements and a fixed 16-entry cache of heard channel nodes.
- The 16-byte Meshtastic radio header, channel hash, AES-128/256-CTR channel
  encryption, protobuf Data/User messages and a 64-entry sender/packet-ID cache.
- LongFast, MediumFast and ShortFast presets: 250 kHz, spreading factors 11, 9
  and 7 respectively, coding rate 4/5, sync word `0x2b`, 16-symbol preamble and CRC.
- US frequency slots 1–104. Slot 0 selects the upstream channel-name-derived
  slot; LongFast on its default channel selects slot 20, 906.875 MHz.
- One pending transmission, randomized channel-access delay and asynchronous
  CAD. A busy channel permits at most four channel checks before reporting that
  the message was not sent. A transmitted packet is never automatically replayed.

The node advertises the non-relaying CLIENT_MUTE role. Private/direct messages,
PKI, acknowledgements, retransmission, forwarding, routing tables, remote admin,
MQTT, phone companion protocol, telemetry and location sharing are not
implemented. Other Meshtastic nodes can forward these broadcasts within the
packet's hop limit; this device does not relay other nodes' packets. “Transmitted”
confirms local radio completion and does not establish reception by a peer.
Unsupported packets are ignored; this build does not claim to be the full
Meshtastic firmware or an official Meshtastic device implementation.

## Keys and radio suspension

The default LongFast key is the **well-known public Meshtastic default key**.
Anyone can read or forge traffic using it. A custom channel accepts an exact
16-byte or 32-byte key, entered as 32 or 64 hexadecimal characters. All peers must
use the same channel name and key. Keys remain in device NVS and are not printed
in diagnostics; flash encryption is not enabled. Keep flash backups private.

Meshtastic channel AES-CTR does not authenticate a sender or provide an
integrity tag. A successful decode is not proof of identity, and the small
channel hash is only a decoding hint. The firmware sends an explicit zero Data
bitfield to withhold MQTT-forwarding consent; other devices' behavior cannot be
guaranteed by that flag.

Airplane mode immediately disables the SX1262 power rail, asserts reset, cancels
pending channel checks/transmission and blocks send, receive/reinitialization
and announcements. The saved Mesh enable preference is retained. Leaving
Airplane mode resumes the receiver only if Mesh was enabled and a valid region
was configured; canceled messages are not replayed.

## Protocol and verification

Interoperability is pinned to official Meshtastic firmware release
[v2.7.26.54e0d8d](https://github.com/meshtastic/firmware/releases/tag/v2.7.26.54e0d8d),
commit `54e0d8d0ab2ff56b3a9ce967e53f79e49af560fb`, and its exact protobuf submodule
commit `6b1ded439633cd03d4af85b44231b91d1d106278`.

Primary references are the pinned
[radio header](https://github.com/meshtastic/firmware/blob/54e0d8d0ab2ff56b3a9ce967e53f79e49af560fb/src/mesh/RadioInterface.h),
[radio/frequency configuration](https://github.com/meshtastic/firmware/blob/54e0d8d0ab2ff56b3a9ce967e53f79e49af560fb/src/mesh/RadioInterface.cpp),
[channel name/key/hash rules](https://github.com/meshtastic/firmware/blob/54e0d8d0ab2ff56b3a9ce967e53f79e49af560fb/src/mesh/Channels.cpp),
[AES-CTR nonce convention](https://github.com/meshtastic/firmware/blob/54e0d8d0ab2ff56b3a9ce967e53f79e49af560fb/src/mesh/CryptoEngine.cpp),
[Data/User schema](https://github.com/meshtastic/protobufs/blob/6b1ded439633cd03d4af85b44231b91d1d106278/meshtastic/mesh.proto)
and official [preset documentation](https://meshtastic.org/docs/overview/radio-settings/).
The protocol adaptation is GPL-3.0-only; see the project's third-party notices.

Host ASan/UBSan tests compare header/Data/User/nonce bytes against independently
generated official-schema fixtures, exercise channel/key/region bounds and
cache eviction, and process 10,000 malformed packets. The fixture generator uses
the official protobuf compiler and Python cryptography AES-CTR, independently
of the firmware's hand codec. Native crypto runs known-answer tests for the
public AES-128 key, custom AES-256 and a multi-block counter before RF startup.
Native source syntax has been checked against the pinned ESP-IDF toolchain.

**A physical Meshtastic peer test has not been performed.** Radio reception,
peer-visible node discovery, channel text delivery and CAD behavior still need
an over-the-air interoperability test. No mesh transmission was performed while
implementing these fixtures. Host fixtures do not establish RF interoperability.

To regenerate developer fixtures, obtain the exact protobuf commit above, then
use the project virtual environment with `grpcio-tools==1.78.0`,
`protobuf==6.33.6` and `cryptography==50.0.2` installed:

```sh
.venv/bin/python tools/generate_mesh_fixtures.py --reference /path/to/pinned/protobufs --check
```

These are optional development tools, not firmware or installer dependencies.
