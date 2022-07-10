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


uint8_t buf[32];


void respond_with_success(void* p) {
   for(;;) {
      vTaskDelay(pdMS_TO_TICKS(5000));
      lora_send_packet((uint8_t*)"Hello", 5);
      ESP_LOGI("main", "Handshake confirmed...");
   }
}

void respond_with_error(void* p) {
   for(;;) {
      vTaskDelay(pdMS_TO_TICKS(5000));
      lora_send_packet((uint8_t*)"Hello", 5);
      ESP_LOGI("main", "Handshake unconfirmed");
   }
}

void task_rx(void *p)
{
   int x;
   for(;;) {
      lora_receive();    // put into receive mode
      while(lora_received()) {
         x = lora_receive_packet(buf, sizeof(buf));

         // x error checking
         // if () {}
         // else {}

         buf[x] = 0;
         ESP_LOGI("main", "Received packet: %s", buf);
         lora_receive();

         respond_with_success();
      }
      vTaskDelay(1);
   }
}

void app_main()
{
   lora_init();
   lora_set_frequency(915e6);
   lora_enable_crc();

   ESP_LOGI("main", "LoRa Enabled");

   xTaskCreate(&task_rx, "task_rx", 2048, NULL, 5, NULL);
}