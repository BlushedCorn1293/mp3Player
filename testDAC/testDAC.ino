#include <Arduino.h>
#include <driver/i2s_std.h>
#include <math.h>

#define I2S_WS    15
#define I2S_DOUT   8
#define I2S_BCLK   7

i2s_chan_handle_t tx_handle;

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("PCM5100 I2S Test Starting...");

  // Create I2S channel
  i2s_chan_config_t chan_cfg =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);

  ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, NULL));

  // Clock (44.1kHz audio)
  i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100);

  // Slot config (stereo 16-bit)
  i2s_std_slot_config_t slot_cfg = {
    .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
    .slot_bit_width = I2S_SLOT_BIT_WIDTH_16BIT,
    .slot_mode = I2S_SLOT_MODE_STEREO,
    .slot_mask = I2S_STD_SLOT_BOTH,
    .ws_width = I2S_DATA_BIT_WIDTH_16BIT,
    .ws_pol = false,
    .bit_shift = true
  };

  // GPIO mapping (UPDATED to your wiring)
  i2s_std_gpio_config_t gpio_cfg = {
    .mclk = I2S_GPIO_UNUSED,
    .bclk = (gpio_num_t)I2S_BCLK,   // GPIO7
    .ws   = (gpio_num_t)I2S_WS,     // GPIO15
    .dout = (gpio_num_t)I2S_DOUT,   // GPIO8
    .din  = I2S_GPIO_UNUSED,
    .invert_flags = {
      .mclk_inv = false,
      .bclk_inv = false,
      .ws_inv = false
    }
  };

  // Combine config
  i2s_std_config_t std_cfg = {
    .clk_cfg = clk_cfg,
    .slot_cfg = slot_cfg,
    .gpio_cfg = gpio_cfg
  };

  ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &std_cfg));
  ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));

  Serial.println("I2S started");
}

void loop() {
  static int16_t buffer[256];
  static float phase = 0;

  for (int i = 0; i < 128; i++) {
    int16_t sample = sinf(phase) * 12000;

    buffer[i * 2]     = sample; // Left
    buffer[i * 2 + 1] = sample; // Right

    phase += 2.0f * PI * 440.0f / 44100.0f;
    if (phase > 2 * PI) phase -= 2 * PI;
  }

  size_t written;
  i2s_channel_write(tx_handle, buffer, sizeof(buffer), &written, portMAX_DELAY);
}