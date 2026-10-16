# Connectivity setup

Open Settings from Home with **S**. Wi-Fi and BLE are disabled by default;
each radio has its own saved enable switch. Cellular scanning does not change
operator selection. The radios serve different purposes: Wi-Fi provides a local
network connection, BLE connects nearby accessories, and the A7682E provides
cellular service when compatible network service is available.

The current source uses **ESP-IDF 5.4.0 directly**: `esp_wifi`, `esp_event` and
`esp_netif` for Wi-Fi; the SDK's native NimBLE host for BLE; and RadioLib's
portable C++ HAL over the native shared SPI bus for the SX1262. No Arduino
framework or Arduino wireless wrapper is required. Wi-Fi credentials stay in
the existing application NVS settings; the driver uses RAM configuration.
BLE bonds use the SDK's `nimble_bond` NVS namespace. The port does not erase
the NVS partition. Native boot, Wi-Fi reconnection, LTE registration, AI transport and LoRa
initialization were retested; the user confirmed a phone call worked. BLE
pairing remains deferred. See the bench log for each test revision.

## Cellular carriers: Settings → Connections → Cellular → Carriers

The modem scans with `AT+COPS=?`, which can take up to three minutes. The UI
continues running while the cellular task owns that operation. Results show the
operator name, numeric network code, radio technology, and Available / Current /
Forbidden / Unknown status. Use J/K for pages and R to repeat the scan.

Detecting a network does not mean the SIM can register on it. A scan does not
select a carrier or establish SMS, voice or mobile data. Other cellular actions
must wait for the scan to finish.

USB: `carriers` starts a scan; `carriers results` opens and prints cached results.
`modem AT+CGSN` reads the cellular modem's IMEI over USB. It is queried only on
request and is not included in boot diagnostics or companion status.
`modem AT+CIMI` reads the SIM's IMSI on request; keep that identifier private.
`modem AT+CMGL=4` explicitly dumps raw SIM SMS PDUs over USB for codec diagnosis.
It may mark messages as read but does not delete them; keep its output private.

SMS setup prefers **`CPMS="SM","SM","ME"`**: the default reading store and
stored-message sending store remain SIM (SM), while new received messages are
stored in modem memory (ME). This avoids depending on free slots in a full SIM.
If the modem rejects ME reception, setup falls back to SM/SM/SM and reports that
fallback. Transient SIM-busy errors retry setup instead. A full receiving store
reported by CPMS produces a warning; the firmware never deletes messages to make
room. ME can eventually fill too.

The hardware pass before the native port verified SM/SM/ME with the SIM still full at 5/5 and
modem storage empty at 0/180; all existing carrier messages were preserved. After the user sent a fresh reply and reconnected the device, both-store
inspection still found ME empty. Native firmware also verified PDU mode 0,
CNMI 2,1,0,0,0 and CATR 0 (URCs on all interfaces). Incoming delivery remains
unconfirmed; no receiving-code rejection is established by an empty store.

Supported incoming messages are imported from **both SM and ME** at startup,
when Diagnostics I / USB `scan-sim` requests an inbox import, and after message
notifications. The existing `scan-sim` command name is retained for compatibility.
ME notifications schedule a bounded streaming scan; the read store is restored
to SM before any SIM slot is read. Unsupported multipart/sender/encoding messages
remain in their original store. Automatic scans do not print raw PDUs.

`modem inbox-me` explicitly inspects the modem's **ME** message storage. It
temporarily selects ME for CPMS **mem1** (the store used by CMGL/CMGR), streams
the raw PDUs to USB, imports supported incoming messages and restores mem1 to
**SM**. It leaves mem2, the mem3 receiving preference and CNMI unchanged. The
command refuses to interrupt a call, SMS or other modem operation. It sends no
messages and deletes none, but reading can mark stored messages as read. Raw
output can contain message contents and phone numbers; keep it private.

SIM message notifications received during that inspection are deferred until
SM is restored. If restoration fails, slot reads remain blocked while normal
SMS setup repairs the storage selection; a timeout reconnects and reinitializes
the modem. Failed UI delivery or temporary read errors leave a scan pending for
a bounded retry; an outgoing SMS is never automatically resubmitted. Use
`modem AT+CPMS?` afterward to inspect the selected stores. Normally mem1/mem2 are
SM and mem3 is ME, or all three are SM after fallback. Only the explicit
`modem inbox-me` diagnostic prints raw ME PDUs.

After swapping a SIM, `modem restart` requests one software restart of the
cellular module and reinitializes its SIM, SMS setup and saved APN. It refuses
while a call, SMS, scan or another command is active. A restart is never retried
automatically. `diag` and Settings → Connections → Cellular → Refresh status without restarting the module.
`modem auto` requests automatic operator selection (`AT+COPS=0`) and then
refreshes registration status. It can take three minutes and does not start
unless the SIM is ready and the modem is idle. This operation is explicit;
booting or scanning carriers does not change operator selection.

For an AT&T consumer LTE phone SIM, AT&T lists `NXTGENPHONE`; its data-device
profile uses `Broadband`, and IoT plans may have a separately provisioned APN.
Use the APN supplied for the SIM's plan. See [AT&T's data settings](https://www.att.com/support/article-modal/wireless/KM1062162/).
An accepted APN does not establish network registration or device eligibility.

The top bar shows the cellular technology (`LTE`, `2G`, `3G`, or `NET` when
unknown), four signal bars and battery percentage. Bars are a coarse CSQ/RSSI
display: 1 below -95 dBm, 2 from -95, 3 from -85, and 4 from -75 dBm. Unknown
signal leaves the bars empty. `...` means checking/searching, `SIM` means the
SIM is not ready, `NO` means unregistered/denied, `SOS` means emergency-only,
and `SMS` means SMS-only registration. Disconnected states never retain filled
bars from an earlier connection. This indicator describes cellular service.

Beside it, Wi-Fi has a separate **three-bar** indicator: `Woff` is disabled,
`W--` is enabled/disconnected, `W...` is connecting and `WiFi` is connected.
Known Wi-Fi RSSI below -75 dBm gives one bar, -75 through below -60 gives two,
and -60 or stronger gives three. Unknown RSSI leaves the bars empty. A Wi-Fi
association or an LTE registration label alone does not prove Internet access.

## Wi-Fi: Settings → Connections → Wi-Fi

The ESP32-S3 supports **2.4 GHz** Wi-Fi. W toggles the radio; R scans; J/K select
a result; Enter opens its password entry, or connects directly to an open
network. Enter again saves and connects. Password text is masked on screen and
is not printed by the USB controls. N enters a hidden network name manually.
Personal WPA/WPA2/WPA3 networks and open networks are supported; enterprise and
WEP setup are not implemented. A password is 8–63 printable characters or a
64-digit hexadecimal PSK. The keyboard editor uses printable ASCII; scanned
network names retain their original bytes.

The selected network and credentials are saved in NVS and reconnect at boot when
Wi-Fi is enabled. F, followed by Y, forgets the saved network and turns Wi-Fi off.
A connection attempt times out after 30 seconds; selecting the same network
again explicitly retries. The screen shows the DHCP address after connection.
A connected Wi-Fi link does not itself demonstrate Internet access or provide a
browser, hotspot or captive-portal login.

Credentials are stored on the device without application-level encryption;
this hobby build does not enable flash encryption. Keep flash backups private.
Avoid raw-key tracing while entering passwords. Passwords have no USB-console
set/get command and are never exposed through the BLE companion service.

USB: `wifi`, `wifi on`, `wifi off`, `wifi scan`, `wifi networks`, `wifi forget`.
`status` reports connection state and IP, not the password.

## AI data connection

The unified Messages list includes one **AI Assistant** thread. Type directly
into its composer and press Enter or tap Send. It uses connected Wi-Fi first;
when Wi-Fi is disconnected, it uses the modem's ready LTE data path. Turn Wi-Fi
off with Settings W or USB `wifi off` to force an LTE test. A request uses its
selected transport once; a failure is not silently retried on the other path.

The thread displays connection/key readiness and the active request transport.
An activated SIM, compatible cellular service, appropriate APN, working packet
data and provider credentials are needed for LTE. Network registration and
signal bars do not alone establish those requirements.

Both paths use verified HTTPS. For LTE, ESP32 mbedTLS validates the certificate
chain and hostname while a queued modem TCP transport carries encrypted bytes.
The dedicated cellular task remains the only UART owner. Diagnostic/network
changes that could disrupt an open AI connection are refused until it closes;
SMS notifications are retained and modem commands remain serialized. Native
modem HTTPS/TLS commands are unused. See [AI_CHAT.md](AI_CHAT.md) for provider
setup, the four model presets plus Custom and the 500-character reply limit.

Before the native port, provider replies passed on hardware over Wi-Fi and, in the later combined
build, over LTE with Wi-Fi explicitly disabled. The user confirmed the requested
tap Messages → AI Assistant → compose → Send workflow and the LTE reply; USB
status also reported Reply received. The saved provider key survived an upload
and USB reset. The detected touch controller was CST3530 240×320. Complete
model-picker/history/touch coverage, deliberate reply truncation and negative
certificate/hostname tests remain unperformed on hardware.

## Bluetooth LE: Settings → Connections → Bluetooth

The ESP32-S3 supports **Bluetooth Low Energy**, not Bluetooth Classic. B toggles
BLE. Bluetooth enable state, keyboard identity address and BLE bonds survive
restarts. Bluetooth remains off after the bench session at the user's request;
keyboard/phone pairing tests were deferred.

### Keyboard

Put a BLE HID keyboard into pairing mode, press R to scan, select it with J/K,
and press Enter. Results advertising HID are marked as keyboard candidates.
Other named BLE devices may appear; selecting one without a compatible HID
keyboard service reports an unsupported keyboard rather than treating its data
as keystrokes. C reconnects the saved keyboard; a saved keyboard is also tried
once at boot when BLE is enabled. F/Y forgets its address and bond.

The native NimBLE host uses the HID-over-GATT service (`1812`), boot keyboard protocol when
available, and parsed report-mode key arrays or NKRO bitmaps otherwise. Supported
reports are bounded to 32 bytes; service discovery accepts up to 32
characteristics and the report map is bounded to 1,024 bytes. Larger or
unsupported layouts are reported as unsupported. Input uses a US ASCII layout, Shift and Caps
Lock, Enter, Escape, Backspace and Space. Escape returns Home. Ctrl/Alt/GUI
shortcuts are not converted to text; held-key repeat and media keys are not
implemented. A keyboard's advertised Bluetooth version alone does not establish
that it supports BLE HID; Bluetooth Classic HID cannot work on this chip.

### Phone companion

P makes **Handheld OS** visible for two minutes. Use a BLE GATT / Nordic UART
client on the phone. This provides a companion transport and read-only device
status; there is no custom phone app or SMS synchronization yet.

The device requests authenticated, encrypted pairing. The pairing screen shows a
numeric comparison (Y confirms, N rejects), a passkey to type on the peer, or a
field for a six-digit code shown by the peer. Sym cancels; prompts expire after
60 seconds. An unauthenticated phone connection is disconnected after 60 seconds.
No pairing code is automatically approved. X/Y clears all BLE bonds and the
saved keyboard. That also requires the phone to pair again.
The bond store does not silently discard an existing peer to admit a new one;
use the explicit Forget controls when it is full.

| GATT service / characteristic | UUID / behavior |
| --- | --- |
| Device Information / firmware revision | `180A` / `2A26`; read Handheld OS version |
| Battery Service / battery level | `180F` / `2A19`; read percentage |
| Nordic UART Service | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` |
| UART RX | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E`; authenticated write |
| UART TX | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E`; authenticated read and notifications |

After pairing, subscribe to TX and write `status` to RX (optional CR/LF). The
response reports OS version, battery, LTE registration, Wi-Fi state and LoRa
readiness. `help` requests the same status. The latest snapshot can also be read
from TX. No messaging, dialing, APN, credentials, inbox or remote-control commands
are accepted. The BLE stack permits concurrent keyboard/phone connections; that combination
has not been bench-tested.

USB: `ble`, `ble on`, `ble off`, `ble scan`, `ble devices`, `ble pair`,
`ble reconnect`, `ble connect <result-number>`. A worker dispatches requests to
the native NimBLE host task, which owns scanning, connections, security, GATT
discovery and notifications. Callbacks forward bounded events to the main UI.
Disabling BLE stops and deinitializes the host/controller while keeping bonds.

## Meshtastic channel

Open Settings → Messaging → Meshtastic. Choose the supported US region only
for a US 915 MHz device, then match preset, channel and key with the peer before
enabling. This is a native channel-text leaf client; it does not relay packets
or provide private direct messages. The public default key provides no privacy.
No node announcement or message is sent automatically. A second-device radio
interoperability test is still required; see [MESH.md](MESH.md).

## Airplane mode and GPS

Settings → Airplane mode stops cellular, Wi-Fi, BLE, LoRa and the GNSS receiver.
The screen reports shutdown progress and radio controls stay gated while it is
on. Saved radio preferences are preserved for resuming; interrupted sends and
calls are not replayed.

Settings → Location enables GPS and shows fix status. Sharing requires a fresh
fix and prepares an SMS draft with a Google Maps link. Choose the recipient,
review the draft and tap Send. GPS reception needs a clear view of the sky; the
parser tests do not establish a live satellite fix. See [GPS.md](GPS.md).
