
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

#define ENCODER_A_GPIO GPIO_NUM_18
#define ENCODER_B_GPIO GPIO_NUM_19
#define ENCODER_BUTTON_GPIO GPIO_NUM_21

static volatile int16_t encoder_count = 0;
static volatile int8_t last_state_encoded = 0;
static volatile int8_t state_encoded = 0;
static volatile int button_pressed = 0;
static volatile int64_t button_press_time = 0;

SemaphoreHandle_t button_semaphore;
TaskHandle_t task1_handle = NULL;
TaskHandle_t task2_handle = NULL;

static void IRAM_ATTR encoder_isr_handler(void *arg) {
    int8_t state_encoded = gpio_get_level(ENCODER_A_GPIO);
    if (state_encoded != last_state_encoded) {
        if (gpio_get_level(ENCODER_B_GPIO) != state_encoded) {
            encoder_count++;
        } else {
            encoder_count--;
        }
    }
    last_state_encoded = state_encoded;
}

static void IRAM_ATTR button_isr_handler(void *arg) {
    if (gpio_get_level(ENCODER_BUTTON_GPIO) == 0) {
        button_press_time = esp_timer_get_time();
    } else {
        int64_t press_duration = esp_timer_get_time() - button_press_time;
        if (press_duration > 3000000) { // 3 seconds in microseconds
            button_pressed = 2; // Long press
        } else {
            button_pressed = 1; // Short press
        }
        xSemaphoreGiveFromISR(button_semaphore, NULL);
    }
}

void task1(void *pvParameter) {
    while (1) {
        printf("Task 1: Doing some work...\n");
        vTaskDelay(pdMS_TO_TICKS(1000)); // Delay for a second
    }
}

void task2(void *pvParameter) {
    while (1) {
        if (xSemaphoreTake(button_semaphore, portMAX_DELAY)) {
            if (button_pressed == 1) {
                printf("Task 2: Button pressed! Encoder count: %d\n", encoder_count);
                button_pressed = 0;
            } else if (button_pressed == 2) {
                printf("Task 2: Button long pressed! Entering count update mode.\n");
                button_pressed = 0;

                // Suspend task 1
                vTaskSuspend(task1_handle);

                // Update encoder count every second until button is pressed again
                while (1) {
                    printf("Task 2: Updating every second. Encoder count: %d\n", encoder_count);
                    vTaskDelay(pdMS_TO_TICKS(1000)); // Update every second
                    if (gpio_get_level(ENCODER_BUTTON_GPIO) == 0) {
                        // Wait for the button to be released
                        while (gpio_get_level(ENCODER_BUTTON_GPIO) == 0) {
                            vTaskDelay(pdMS_TO_TICKS(10));
                        }
                        printf("Task 2: Button pressed again! Resuming Task 1.\n");
                        vTaskResume(task1_handle);
                        break;
                    }
                }
            }
        }
    }
}

void app_main(void) {
    // Configure GPIOs for rotary encoder
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_ANYEDGE; // Interrupt on both rising and falling edges
    io_conf.mode = GPIO_MODE_INPUT; // Set as input mode
    io_conf.pin_bit_mask = (1ULL << ENCODER_A_GPIO) | (1ULL << ENCODER_B_GPIO); // Set pins
    io_conf.pull_down_en = 0; // Disable pull-down mode
    io_conf.pull_up_en = 1; // Enable pull-up mode
    gpio_config(&io_conf);

    // Configure GPIO for button
    gpio_config_t button_conf = {};
    button_conf.intr_type = GPIO_INTR_ANYEDGE; // Interrupt on both rising and falling edges
    button_conf.mode = GPIO_MODE_INPUT; // Set as input mode
    button_conf.pin_bit_mask = (1ULL << ENCODER_BUTTON_GPIO); // Set pin
    button_conf.pull_down_en = 0; // Disable pull-down mode
    button_conf.pull_up_en = 1; // Enable pull-up mode
    gpio_config(&button_conf);

    // Install ISR service
    gpio_install_isr_service(0);
    gpio_isr_handler_add(ENCODER_A_GPIO, encoder_isr_handler, (void *)ENCODER_A_GPIO);
    gpio_isr_handler_add(ENCODER_B_GPIO, encoder_isr_handler, (void *)ENCODER_B_GPIO);
    gpio_isr_handler_add(ENCODER_BUTTON_GPIO, button_isr_handler, (void *)ENCODER_BUTTON_GPIO);

    // Create semaphore for button press
    button_semaphore = xSemaphoreCreateBinary();

    // Create tasks
    xTaskCreate(&task1, "task1", 2048, NULL, 5, &task1_handle);
    xTaskCreate(&task2, "task2", 2048, NULL, 5, &task2_handle);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000)); // Main loop delay for a second
    }
}
