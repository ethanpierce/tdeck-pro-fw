file(GLOB APP_SOURCES "${CMAKE_CURRENT_LIST_DIR}/*.cpp")
file(GLOB CORE_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../lib/DeckCore/src/*.cpp")
idf_component_register(SRCS ${APP_SOURCES} ${CORE_SOURCES}
    INCLUDE_DIRS "." "../config" "../lib/DeckCore/src"
    REQUIRES driver esp_timer esp_system esp_psram esp_wifi esp_event esp_netif
             nvs_flash esp_http_client esp-tls mbedtls bt lwip)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
