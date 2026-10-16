# GPS and location sharing

Open **Settings → Location** to turn GPS on. It is off by default. The saved preference controls later boots; Airplane mode temporarily powers GPS off and restores that preference when Airplane mode ends.

For a first fix, take the device outdoors with a clear view of the sky and allow several minutes. The page shows receiver status, fresh coordinates, satellites, UTC and HDOP when available. HDOP is dimensionless, not an accuracy in meters. A horizontal uncertainty estimate is shown only if the receiver supplies a fresh GST sentence. The six decimal places in a map link describe formatting, not guaranteed positioning accuracy.

Choose **Share by SMS** with GPS on and a real fix no more than 30 seconds old. Enter a recipient, review the Google Maps link in the message draft, then explicitly choose **Send**. Preparing a link sends nothing. Standard SMS service and any carrier charges apply. Sent location links appear in normal message history. Estimated, manually entered, simulated, missing, invalid or stale fixes cannot create a location link.

The receiver service keeps its fix in RAM. It does not automatically send, log, or persist coordinates, and does not broadcast location over LoRa or Bluetooth. Turning GPS off clears the service's current fix. GPS time is displayed without changing the system clock.

## Native implementation and hardware

The supported v1.0/v1.1 boards route GNSS TX to ESP GPIO44 and GNSS RX to GPIO43, with its power rail controlled by GPIO39. The native service uses UART2 in receive-only mode: GPIO43 remains an input, and it sends no UBX commands, NMEA configuration, factory reset or recovery sequence. On shutdown the power rail goes low, the UART driver is removed, and RX/TX pads return to floating GPIO inputs.

The MIA-M10Q default is **38400 baud, 8N1**. If no checksummed NMEA sentence arrives after eight seconds, the host UART alternates between 38400 and 9600 baud until valid data arrives. This supports the vendor's alternate receiver/retained-baud path without reprogramming the module. A receiver configured to output only UBX or another baud rate will report no GPS data.

Input is bounded to a 128-byte sentence buffer and 1024 received bytes per UI poll. The UART receive buffer is 2048 bytes. GGA and RMC provide fix data; GST optionally supplies error estimates. Bad checksums, oversized sentences and invalid coordinates are discarded. Fixes, UTC and accuracy details expire after 30 seconds. GGA qualities 1–5 and normal GNSS RMC modes qualify for sharing; dead-reckoning/manual/simulation modes do not.

The service and parser use the project's GPL-3.0-only license. No Arduino framework, TinyGPS++ library or new external dependency is used.

Primary references:

- [LILYGO v1.1 pinned GPS implementation](https://github.com/Xinyuan-LilyGO/T-Deck-Pro/blob/02b3e82a64926a1d9f907f3b5538a40d1e48795c/examples/factory/peri_gps.cpp) and [pin definitions](https://github.com/Xinyuan-LilyGO/T-Deck-Pro/blob/02b3e82a64926a1d9f907f3b5538a40d1e48795c/examples/factory/utilities.h).
- [LILYGO v1.0 pinned pin definitions](https://github.com/Xinyuan-LilyGO/T-Deck-Pro/blob/e3246046972716edd5f264a6047ba3572a851b23/examples/factory/utilities.h).
- [u-blox MIA-M10Q datasheet](https://content.u-blox.com/sites/default/files/documents/MIA-M10Q_DataSheet_UBX-22015849.pdf) and [M10 SPG interface description](https://content.u-blox.com/sites/default/files/u-blox-M10-SPG-5.10_InterfaceDescription_UBX-21035062.pdf).
- [Google Maps URLs](https://developers.google.com/maps/documentation/urls/get-started): the share link uses `https://www.google.com/maps/search/?api=1&query=LAT%2CLON` without an API key.

Physical receiver acquisition and a recipient-opened map link still require hardware testing; parser and lifecycle simulations do not establish an outdoor fix.
