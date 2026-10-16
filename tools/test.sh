#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
flags=(-std=c++17 -Wall -Wextra -Werror -g -O1
  -fsanitize=address,undefined -fno-omit-frame-pointer -Ilib/DeckCore/src)
"${CXX:-g++}" "${flags[@]}" tests/core_test.cpp lib/DeckCore/src/*.cpp -o build/core_test
"${CXX:-g++}" "${flags[@]}" -Itests/fakes -Isrc tests/store_test.cpp \
  src/message_store.cpp lib/DeckCore/src/sms_pdu.cpp lib/DeckCore/src/at_engine.cpp -o build/store_test
"${CXX:-g++}" "${flags[@]}" -Itests/fakes -Iconfig tests/modem_service_test.cpp \
  lib/DeckCore/src/sms_pdu.cpp lib/DeckCore/src/at_engine.cpp lib/DeckCore/src/apn.cpp -o build/modem_service_test
"${CXX:-g++}" "${flags[@]}" -Itests/fakes -Isrc tests/settings_test.cpp \
  src/device_settings.cpp lib/DeckCore/src/apn.cpp lib/DeckCore/src/sms_pdu.cpp \
  lib/DeckCore/src/at_engine.cpp -o build/settings_test
# LeakSanitizer cannot attach under some sandbox ptrace policies; ASan/UBSan run normally.
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" ./build/core_test
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" ./build/store_test
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" ./build/modem_service_test
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" ./build/settings_test
