/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
/* STEP 3 - Include the header file of the Bluetooth LE stack */
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>

LOG_MODULE_REGISTER(BLUE, LOG_LEVEL_INF);

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

#define RUN_LED_BLINK_INTERVAL_MS 1000

/* STEP 4.1.1 - Declare the advertising packet */
static const struct bt_data ad[] = {
    /* STEP 4.1.2 - Set the advertising flags */
    BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
    /* STEP 4.1.3 - Set the advertising packet data  */
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),

};

/* STEP 4.2.2 - Declare the URL data to include in the scan response */
static unsigned char url_data[] = {0x17, '/', '/', 'f', 'u', 'c', 'k', '-', 'b',
                                   'e', 't', 'i', 'c'};

/* STEP 4.2.1 - Declare the scan response packet */
static const struct bt_data sd[] = {
    /* 4.2.3 Include the URL data in the scan response packet */
    BT_DATA(BT_DATA_URI, url_data, sizeof(url_data)),
};

int main(void)
{
    int err;

    if (!device_is_ready(led.port))
    {
        printk("Error: LED device %s is not ready\n", led.port->name);
        return -1;
    }

    err = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
    if (err)
    {
        printk("Error %d: Failed to configure LED pin\n", err);
        return -1;
    }

    /* STEP 5 - Enable the Bluetooth LE stack */
    err = bt_enable(NULL);
    if (err)
    {
        LOG_ERR("Bluetooth init failed (err %d)\n", err);
        return -1;
    }

    LOG_INF("Bluetooth initialized\n");

    /* STEP 6 - Start advertising */
    err = bt_le_adv_start(BT_LE_ADV_NCONN, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err)
    {
        LOG_ERR("Advertising failed to start (err %d)\n", err);
        return -1;
    }

    LOG_INF("Advertising successfully started\n");

    bool led_is_on = true;

    while (1)
    {
        gpio_pin_set_dt(&led, (int)led_is_on);
        led_is_on = !led_is_on;
        k_sleep(K_MSEC(RUN_LED_BLINK_INTERVAL_MS));
    }

    return 0;
}