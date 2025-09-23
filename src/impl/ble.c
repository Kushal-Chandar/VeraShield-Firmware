#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/kernel.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/services/bas.h>

#include "ble.h"
#include "cycle.h"
#include "slider.h"
#include "manual_spray.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(BLE, LOG_LEVEL_INF);

static void on_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    // if (value == BT_GATT_CCC_NOTIFY)
    // {
    // }
    // else if (value == BT_GATT_CCC_INDICATE)
    // {
    // }
    // else
    // {
    // }
    // Eat 5 - Star - Do Nothing
}

static ssize_t schedule_read(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                             uint16_t len, uint16_t offset)
{

    return 0;
}

static ssize_t schedule_write(struct bt_conn *conn,
                              const struct bt_gatt_attr *attr,
                              const void *buf, uint16_t len,
                              uint16_t offset, uint8_t flags)
{
    if (offset != 0)
    {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    if (len == 0)
    {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    // do_shit(buf, len);
    do_shit();
    return len; /* must return number of bytes “consumed” */
}

static ssize_t statistics_read(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                               uint16_t len, uint16_t offset)
{

    return 0;
}

void do_shit()
{
    spray_action();
}

static ssize_t remote_spray_write(struct bt_conn *conn,
                                  const struct bt_gatt_attr *attr,
                                  const void *buf, uint16_t len,
                                  uint16_t offset, uint8_t flags)
{
    if (offset != 0)
    {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    if (len == 0)
    {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    // do_shit(buf, len);
    do_shit();
    return len; /* must return number of bytes “consumed” */
}

BT_GATT_SERVICE_DEFINE(
    machhar_svc,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_MACHHAR_SERVICE),
    BT_GATT_CHARACTERISTIC(BT_UUID_MACHHAR_SCHEDULING, BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP | BT_GATT_CHRC_READ,
                           BT_GATT_PERM_WRITE, schedule_read, schedule_write, NULL),
    BT_GATT_CHARACTERISTIC(BT_UUID_MACHHAR_STATISTICS, BT_GATT_CHRC_READ,
                           BT_GATT_PERM_WRITE, statistics_read, NULL, NULL),
    BT_GATT_CHARACTERISTIC(BT_UUID_MACHHAR_REMOTE_SPRAY, BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE, NULL, remote_spray_write, NULL),
    BT_GATT_CCC(on_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE));
