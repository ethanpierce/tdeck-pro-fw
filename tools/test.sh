#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
flags=(-std=c++17 -Wall -Wextra -Werror -g -O1
  -fsanitize=address,undefined -fno-omit-frame-pointer -Ilib/DeckCore/src)
"${CXX:-g++}" "${flags[@]}" tests/core_test.cpp lib/DeckCore/src/*.cpp -o build/core_test
"${CXX:-g++}" "${flags[@]}" -Itests/fakes -Isrc tests/store_test.cpp \
  src/message_store.cpp lib/DeckCore/src/sms_pdu.cpp lib/DeckCore/src/at_engine.cpp -o build/store_test
"${CXX:-g++}" "${flags[@]}" -DDECK_CELLULAR_SOCKET_TEST -Itests/fakes -Iconfig tests/modem_service_test.cpp \
  src/cellular_socket.cpp lib/DeckCore/src/cellular_http_protocol.cpp lib/DeckCore/src/ai_protocol.cpp \
  lib/DeckCore/src/sms_pdu.cpp lib/DeckCore/src/at_engine.cpp lib/DeckCore/src/apn.cpp lib/DeckCore/src/connectivity.cpp -o build/modem_service_test
"${CXX:-g++}" "${flags[@]}" -Itests/fakes -Isrc tests/settings_test.cpp \
  src/device_settings.cpp lib/DeckCore/src/apn.cpp lib/DeckCore/src/sms_pdu.cpp \
  lib/DeckCore/src/at_engine.cpp lib/DeckCore/src/connectivity.cpp lib/DeckCore/src/mesh_protocol.cpp -o build/settings_test
"${CXX:-g++}" "${flags[@]}" tests/connectivity_test.cpp \
  lib/DeckCore/src/connectivity.cpp lib/DeckCore/src/at_engine.cpp -o build/connectivity_test
"${CXX:-g++}" "${flags[@]}" -Itests/fakes -Isrc tests/ai_chat_test.cpp \
  src/ai_settings.cpp lib/DeckCore/src/ai_protocol.cpp -o build/ai_chat_test
"${CXX:-g++}" "${flags[@]}" tests/conversations_test.cpp \
  lib/DeckCore/src/conversations.cpp -o build/conversations_test
"${CXX:-g++}" "${flags[@]}" tests/touch_test.cpp \
  lib/DeckCore/src/touch_packet.cpp -o build/touch_test
"${CXX:-g++}" "${flags[@]}" tests/cellular_http_test.cpp \
  lib/DeckCore/src/cellular_http_protocol.cpp lib/DeckCore/src/ai_protocol.cpp \
  lib/DeckCore/src/at_engine.cpp -o build/cellular_http_test
"${CXX:-g++}" "${flags[@]}" tests/ui_input_test.cpp -o build/ui_input_test
"${CXX:-g++}" "${flags[@]}" tests/ui_layout_test.cpp lib/DeckCore/src/ui_layout.cpp \
  lib/DeckCore/src/mono_canvas.cpp -o build/ui_layout_test
"${CXX:-g++}" "${flags[@]}" tests/power_button_test.cpp -o build/power_button_test
"${CXX:-g++}" "${flags[@]}" -DDECK_POWER_CONTROL_TEST -Itests/fakes/power -Itests/fakes -Isrc \
  tests/power_control_test.cpp -o build/power_control_test
"${CXX:-g++}" "${flags[@]}" -Itests/fakes/display -Isrc tests/native_ui_test.cpp \
  src/native_screen.cpp lib/DeckCore/src/mono_canvas.cpp -o build/native_ui_test
"${CXX:-g++}" "${flags[@]}" tests/mesh_protocol_test.cpp \
  lib/DeckCore/src/mesh_protocol.cpp -o build/mesh_protocol_test
"${CXX:-g++}" "${flags[@]}" tests/radio_gate_test.cpp -o build/radio_gate_test
"${CXX:-g++}" "${flags[@]}" tests/nmea_test.cpp lib/DeckCore/src/nmea.cpp -o build/nmea_test
"${CXX:-g++}" "${flags[@]}" -Itests/fakes/gps -Isrc tests/gps_service_test.cpp \
  src/gps_service.cpp lib/DeckCore/src/nmea.cpp -o build/gps_service_test
# LeakSanitizer cannot attach under some sandbox ptrace policies; ASan/UBSan run normally.
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" ./build/core_test
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" ./build/store_test
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" ./build/modem_service_test
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" ./build/settings_test
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" ./build/connectivity_test
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" ./build/ai_chat_test
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" ./build/conversations_test
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" ./build/touch_test
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" ./build/cellular_http_test
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" ./build/ui_input_test
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" ./build/ui_layout_test
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" ./build/power_button_test
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" ./build/power_control_test
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" ./build/native_ui_test
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" ./build/mesh_protocol_test
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" ./build/radio_gate_test
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" ./build/nmea_test
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" ./build/gps_service_test
