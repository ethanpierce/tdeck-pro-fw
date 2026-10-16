#pragma once
constexpr int ESP_SLEEP_WAKEUP_ALL=0;
int esp_sleep_disable_wakeup_source(int);
void esp_deep_sleep_start();
