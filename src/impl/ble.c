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

#include "stats.h"
#include "tm_helpers.h"
#include "ble.h"
#include "spray.h"
#include "pcf8563.h"

LOG_MODULE_REGISTER(BLE, LOG_LEVEL_INF);

static uint16_t stats_start = 0;
static uint8_t stats_window = 63;

enum
{
    ST_HDR = 2,
    ST_ENTRY = 8,
    ST_MAX_RETURNED = 63
};

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
    const uint8_t total = stats_count();

    uint16_t start = stats_start;
    uint8_t want = stats_window;
    if (start > total)
        start = total;
    uint16_t avail = (uint16_t)(total - start);
    if (want > ST_MAX_RETURNED)
        want = ST_MAX_RETURNED;
    if (want > avail)
        want = (uint8_t)avail;

    const uint32_t full_len = (uint32_t)ST_HDR + (uint32_t)want * ST_ENTRY;

    if (offset > full_len)
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    if (offset == full_len)
        return 0;

    uint16_t to_copy = len;
    if ((uint32_t)offset + to_copy > full_len)
    {
        to_copy = (uint16_t)(full_len - offset);
    }

    uint8_t out[to_copy];
    uint16_t produced = 0;
    uint32_t cur = offset;

    if (cur < ST_HDR)
    {
        const uint8_t hdr[ST_HDR] = {total, want};
        const uint16_t hdr_rem = (uint16_t)(ST_HDR - cur);
        const uint16_t chunk = (hdr_rem < to_copy) ? hdr_rem : to_copy;
        memcpy(out, &hdr[cur], chunk);
        produced += chunk;
        cur += chunk;

        LOG_INF("Stats Read Header: total=%u want=%u (start=%u)", total, want, start);
    }

    while (produced < to_copy)
    {
        const uint32_t entries_off = cur - ST_HDR;
        const uint16_t rel_idx = (uint16_t)(entries_off / ST_ENTRY);
        const uint8_t entry_off = (uint8_t)(entries_off % ST_ENTRY);
        const uint16_t abs_idx = (uint16_t)(start + rel_idx);

        uint8_t time7[7], inten2b = 0;
        if (!stats_get((uint8_t)abs_idx, time7, &inten2b))
        {
            LOG_WRN("stats_get(%u) failed; stopping read build", abs_idx);
            break;
        }

        uint8_t entry_buf[ST_ENTRY];
        memcpy(entry_buf, time7, 7);
        entry_buf[7] = (uint8_t)(inten2b & 0x03);

        const uint16_t entry_rem = (uint16_t)(ST_ENTRY - entry_off);
        const uint16_t space_rem = (uint16_t)(to_copy - produced);
        const uint16_t chunk = (entry_rem < space_rem) ? entry_rem : space_rem;

        memcpy(out + produced, entry_buf + entry_off, chunk);
        produced += chunk;
        cur += chunk;

        // Only log the entry once when we start at its offset 0 (avoids duplicate logs on long reads)
        if (entry_off == 0)
        {
            // Decode to human-readable time
            struct tm tmv = {0};
            tm_from_7(&tmv, time7); // month expected 0..11 per your comment above
            char tsbuf[64];
            (void)tm_to_str(&tmv, tsbuf, sizeof(tsbuf));

            LOG_INF("Stats Entry[%u]: ts=%s  raw=%02x %02x %02x %02x %02x %02x %02x  intensity=%u",
                    abs_idx,
                    tsbuf,
                    time7[0], time7[1], time7[2], time7[3], time7[4], time7[5], time7[6],
                    (unsigned)(inten2b & 0x03));
        }
    }

    LOG_HEXDUMP_INF(out, produced, "Stats Read Outgoing Slice:");

    return bt_gatt_attr_read(conn, attr, buf, len, offset, out, produced);
}

static ssize_t statistics_ctrl_write(struct bt_conn *conn,
                                     const struct bt_gatt_attr *attr,
                                     const void *buf, uint16_t len,
                                     uint16_t offset, uint8_t flags)
{

    LOG_INF("statistics_ctrl_write: handle=0x%04x offset=%u len=%u flags=0x%02x",
            attr ? attr->handle : 0, offset, len, flags);
    LOG_HEXDUMP_INF(buf, len, "Stats Ctrl Write (incoming)");

    if (offset != 0 || len != 2)
    {
        LOG_WRN("Invalid write: offset=%u len=%u (expect 2)", offset, len);
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    const uint8_t *p = buf;
    uint8_t start_req = p[0];
    uint8_t win_req = p[1];

    LOG_INF("Parsed control: start_req=%u window_req=%u", start_req, win_req);

    const uint8_t total = stats_count();

    uint16_t start = start_req;
    if (start > total)
        start = total;

    uint8_t win = (win_req == 0 || win_req > ST_MAX_RETURNED) ? ST_MAX_RETURNED : win_req;

    uint16_t avail = total - start;
    if (win > avail)
        win = (uint8_t)avail;

    stats_start = (uint16_t)start;
    stats_window = win;

    LOG_INF("Effective window: start=%u window=%u (total=%u)", start, win, total);

    return len;
}

static ssize_t schedule_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                              const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    if (offset != 0)
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    if (len == 0)
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);

    /* parse buf,len and apply config if you want; for now just trigger */
    // do_shit();
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
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    else if (rc == -EBADMSG || rc == -ERANGE)
        return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
    else if (rc)
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);

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

    return len;
}

static ssize_t remote_spray_write(struct bt_conn *conn,
                                  const struct bt_gatt_attr *attr,
                                  const void *buf, uint16_t len,
                                  uint16_t offset, uint8_t flags)
{
    if (offset != 0)
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    if (len != 1)
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);

    const uint8_t raw = *((const uint8_t *)buf);
    const uint8_t state = (uint8_t)(raw & 0x03);

    LOG_INF("Remote spray (BLE): req_state=%u", (unsigned)state);

    ble_spray_caller(state);

    return len;
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
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                           statistics_read, statistics_ctrl_write, NULL),
    BT_GATT_CHARACTERISTIC(BT_UUID_MACHHAR_REMOTE_SPRAY,
                           BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE,
                           NULL, remote_spray_write, NULL)

    /* If you add notify on any of the above, put a CCC **right after** that char:
    BT_GATT_CCC(on_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    */
);