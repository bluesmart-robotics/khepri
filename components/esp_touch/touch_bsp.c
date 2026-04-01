#include <stdio.h>
#include "touch_bsp.h"
#include "i2c_bsp.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define I2C_ADDR_FT3168 0x38
static const char *TAG = "TOUCH";

void touch_Init(void)
{
  uint8_t data = 0x00;
  /* Retry : le FT3168 peut ne pas etre pret immediatement apres le boot */
  for (int i = 0; i < 5; i++) {
    if (I2C_writr_buff(I2C_ADDR_FT3168, 0x00, &data, 1) == 0) {
      ESP_LOGI(TAG, "FT3168 touch init OK");
      return;
    }
    ESP_LOGW(TAG, "FT3168 not ready, retry %d/5...", i + 1);
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  ESP_LOGE(TAG, "FT3168 touch init failed — touch may not work");
}
uint8_t getTouch(uint16_t *x,uint16_t *y)
{
  uint8_t data = 0;
  uint8_t buf[4];
  I2C_read_buff(I2C_ADDR_FT3168,0x02,&data,1);
  if(data)
  {
    I2C_read_buff(I2C_ADDR_FT3168,0x03,buf,4);
    *y = (((uint16_t)buf[0] & 0x0f)<<8) | (uint16_t)buf[1];
    *x = (((uint16_t)buf[2] & 0x0f)<<8) | (uint16_t)buf[3];
    return 1;
  }
  return 0;
}