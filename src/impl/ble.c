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
#include <zephyr/bluetooth/att.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/logging/log.h>

#include "statistic.h"
#include "pcf8563.h"
#include "ble.h"
#include "cycle.h"
#include "slider.h"
#include "spray.h"

LOG_MODULE_REGISTER(BLE, LOG_LEVEL_INF);

void do_shit()
{
    struct cycle_cfg_t cfg = {.spray_ms = 5000, .idle_ms = 2000, .repeats = 3};
    spray_action_with_cfg(cfg);
}

static int parse_gadi_time_payload(const uint8_t *buf, uint16_t len, struct tm *out)
{
    if (len != 7)
        return -EMSGSIZE;
    tm_from_7(out, buf); // Note: Please send month from 0 to 11
    char tsbuf[100];
    LOG_INF("RTC: %s", tm_to_str(out, tsbuf, sizeof(tsbuf)));
    return tm_sane(out) ? 0 : -ERANGE;
}

// static void on_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
// {
//     if (value == BT_GATT_CCC_NOTIFY)
//     {
//     }
//     else if (value == BT_GATT_CCC_INDICATE)
//     {
//     }
//     else
//     {
//     }
//     // Eat 5 - Star - Do Nothing
// }

static ssize_t schedule_read(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                             void *buf, uint16_t len, uint16_t offset)
{
    static const uint8_t sched_example[] = {0x01, 0x02};
    return bt_gatt_attr_read(conn, attr, buf, len, offset, sched_example, sizeof(sched_example));
}

static ssize_t statistics_read(struct bt_conn *conn,
                               const struct bt_gatt_attr *attr,
                               void *buf, uint16_t len, uint16_t offset)
{
    uint16_t count = 0;
    uint8_t state = 0;
    struct tm ts;

    int rc = statistic_load(&count, &state, &ts);
    if (rc == -ENOENT)
    {
        LOG_WRN("Statistics not initialized yet");
        return BT_GATT_ERR(BT_ATT_ERR_READ_NOT_PERMITTED);
    }
    if (rc)
    {
        LOG_ERR("statistic_load rc=%d", rc);
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

    uint8_t payload[STAT_LEN_BYTES];
    uint16_t meta = stat_pack(count, state);

    payload[0] = (uint8_t)(meta >> 8);
    payload[1] = (uint8_t)(meta & 0xFF);
    payload[2] = (uint8_t)ts.tm_sec;
    payload[3] = (uint8_t)ts.tm_min;
    payload[4] = (uint8_t)ts.tm_hour;
    payload[5] = (uint8_t)ts.tm_mday;
    payload[6] = (uint8_t)ts.tm_wday;
    payload[7] = (uint8_t)ts.tm_mon;
    payload[8] = (uint8_t)(ts.tm_year - 100);

    LOG_INF("Statistics read: count=%u, state=%u", count, state);
    char tsbuf[100];
    LOG_INF("RTC: %s", tm_to_str(&ts, tsbuf, sizeof(tsbuf)));

    return bt_gatt_attr_read(conn, attr, buf, len, offset,
                             payload, sizeof(payload));
}
static ssize_t schedule_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                              const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    if (offset != 0)
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    if (len == 0)
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);

    /* parse buf,len and apply config if you want; for now just trigger */
    do_shit();
    return len;
}

static ssize_t gadi_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                          const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    if (offset != 0)
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    if (len == 0)
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);

    struct tm t;
    int rc = parse_gadi_time_payload(buf, len, &t);
    if (rc == -EMSGSIZE)
    {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    else if (rc == -EBADMSG || rc == -ERANGE)
    {
        return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
    }
    else if (rc)
    {
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

    struct pcf8563 *rtc = pcf8563_get();
    if (!rtc)
    {
        LOG_ERR("pcf8563_get() returned NULL; not bound yet");
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

    rc = pcf8563_set_time(rtc, &t);
    if (rc)
    {
        LOG_ERR("pcf8563_set_time failed: %d", rc);
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

    char tsbuf[100];
    LOG_INF("RTC: %s", tm_to_str(&t, tsbuf, sizeof(tsbuf)));

    return len; // consumed all bytes
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
    BT_GATT_CHARACTERISTIC(BT_UUID_MACHHAR_GADI_SYNC,
                           BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE,
                           NULL, gadi_write, NULL),
    BT_GATT_CHARACTERISTIC(BT_UUID_MACHHAR_SCHEDULING,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                           schedule_read, schedule_write, NULL),
    BT_GATT_CHARACTERISTIC(BT_UUID_MACHHAR_STATISTICS,
                           BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ,
                           statistics_read, NULL, NULL),
    BT_GATT_CHARACTERISTIC(BT_UUID_MACHHAR_REMOTE_SPRAY,
                           BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE,
                           NULL, remote_spray_write, NULL)

    /* If you add notify on any of the above, put a CCC **right after** that char:
    BT_GATT_CCC(on_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    */
);