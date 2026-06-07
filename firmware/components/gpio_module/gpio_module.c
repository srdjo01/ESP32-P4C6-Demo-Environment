#include "gpio_module.h"

#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "gpio_module";

/* Free GPIO pins available for host control.
 * GPIO14/15 are reserved for the ESP32-C6 UART link (see wifi_module), so the
 * general-purpose set is 16-19. */
static const int FREE_GPIO[] = { 16, 17, 18, 19 };
#define FREE_GPIO_COUNT (sizeof(FREE_GPIO) / sizeof(FREE_GPIO[0]))

#define PIN_ILLUMINATION 26
#define PIN_IGNITION     27

static bool s_init = false;

/* ── Helpers ────────────────────────────────────────────────────────────── */

static bool is_free_gpio(int pin)
{
    for (size_t i = 0; i < FREE_GPIO_COUNT; i++) {
        if (FREE_GPIO[i] == pin) return true;
    }
    return false;
}

/* ── Public API ─────────────────────────────────────────────────────────── */

void gpio_module_init(void)
{
    /* Configure free GPIOs as outputs, default low. */
    for (size_t i = 0; i < FREE_GPIO_COUNT; i++) {
        gpio_reset_pin(FREE_GPIO[i]);
        gpio_set_direction(FREE_GPIO[i], GPIO_MODE_INPUT_OUTPUT);
        gpio_set_level(FREE_GPIO[i], 0);
    }

    /* Configure ignition/illumination as inputs with pull-down.
     * The board uses a level-shifter: the 12/24 V signal is brought down
     * to 3.3 V before reaching the GPIO. */
    gpio_config_t in_cfg = {
        .pin_bit_mask  = (1ULL << PIN_ILLUMINATION) | (1ULL << PIN_IGNITION),
        .mode          = GPIO_MODE_INPUT,
        .pull_up_en    = GPIO_PULLUP_DISABLE,
        .pull_down_en  = GPIO_PULLDOWN_ENABLE,
        .intr_type     = GPIO_INTR_DISABLE,
    };
    gpio_config(&in_cfg);

    s_init = true;
    ESP_LOGI(TAG, "GPIO module ready (free: 16-19, ign: GPIO27, ilum: GPIO26)");
}

int gpio_module_set(int pin, int level)
{
    if (!is_free_gpio(pin)) {
        ESP_LOGW(TAG, "gpio_set: pin %d is not a free GPIO", pin);
        return -1;
    }
    gpio_set_level(pin, level ? 1 : 0);
    return 0;
}

int gpio_module_get(int pin)
{
    if (!is_free_gpio(pin)) {
        ESP_LOGW(TAG, "gpio_get: pin %d is not a free GPIO", pin);
        return -1;
    }
    return gpio_get_level(pin);
}

int gpio_module_get_ignition(void)
{
    return gpio_get_level(PIN_IGNITION);
}

int gpio_module_get_illumination(void)
{
    return gpio_get_level(PIN_ILLUMINATION);
}
