/* Hello World Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <esp_log.h>

#include "lora.h"


void task_tx(void *p) {
   for(;;) {
      vTaskDelay(pdMS_TO_TICKS(5000));
      lora_send_packet((uint8_t*)"Hello", 5);
      ESP_LOGI("main", "Packet sent...");
   }
}

void app_main() {
   lora_init();
   lora_set_frequency(915e6);
   lora_enable_crc();

   ESP_LOGI("main", "LoRa Enabled");

   xTaskCreate(&task_tx, "task_tx", 2048, NULL, 5, NULL);
}