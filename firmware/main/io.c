#include "io.h"

#include <string.h>
#include "board.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "io";

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_exio;
static SemaphoreHandle_t s_lock;
static uint8_t s_out_shadow;

#define TCA9554_INPUT_REG 0x00
#define TCA9554_OUTPUT_REG 0x01
#define TCA9554_CONFIG_REG 0x03

static esp_err_t exio_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_exio, buf, sizeof(buf), 100);
}

static esp_err_t exio_read_reg(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_exio, &reg, 1, val, 1, 100);
}

esp_err_t io_init(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        configASSERT(s_lock);
    }

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = 0,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_bus), TAG, "i2c bus");

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ADDR_TCA9554,
        .scl_speed_hz = I2C_SPEED_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_bus, &dev_cfg, &s_exio), TAG, "exio dev");

    /* 8 个 IO 全配置成输出，默认全部拉低：
     * EXIO8 = 蜂鸣器（高电平响），上电必须为低，否则会一直响
     * EXIO1 = 屏幕复位，EXIO3 = 屏幕片选（低有效），由 lcd_init 按需拉高 */
    s_out_shadow = 0x00;
    ESP_RETURN_ON_ERROR(exio_write_reg(TCA9554_OUTPUT_REG, s_out_shadow), TAG, "exio out");
    ESP_RETURN_ON_ERROR(exio_write_reg(TCA9554_CONFIG_REG, 0x00), TAG, "exio cfg");

    uint8_t in = 0;
    if (exio_read_reg(TCA9554_INPUT_REG, &in) == ESP_OK) {
        ESP_LOGI(TAG, "TCA9554 ready, input=0x%02X", in);
    } else {
        ESP_LOGW(TAG, "TCA9554 not responding");
    }
    return ESP_OK;
}

i2c_master_bus_handle_t io_bus(void)
{
    return s_bus;
}

void io_lock(void)
{
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
}

void io_unlock(void)
{
    if (s_lock) {
        xSemaphoreGive(s_lock);
    }
}

esp_err_t io_exio_set(int pin, int level)
{
    if (pin < 1 || pin > 8) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t mask = (uint8_t)(1u << (pin - 1));

    io_lock();
    if (level) {
        s_out_shadow |= mask;
    } else {
        s_out_shadow &= (uint8_t)~mask;
    }
    esp_err_t err = exio_write_reg(TCA9554_OUTPUT_REG, s_out_shadow);
    io_unlock();
    return err;
}

uint8_t io_exio_state(void)
{
    return s_out_shadow;
}
