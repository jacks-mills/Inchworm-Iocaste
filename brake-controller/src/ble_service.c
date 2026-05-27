#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ble_service, LOG_LEVEL_DBG);

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/services/nus.h>
#include <zephyr/data/json.h>
#include <string.h>

#include "ble_service.h"
#include "request_handler.h"
#include "brake.h"

#define DEVICE_NAME     CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

/* Connection status flag */
volatile bool ble_connected = false;

static struct bt_conn *current_conn = NULL;
static struct bt_gatt_exchange_params mtu_params;

/* NUS notifications enabled flag */
static bool notifications_enabled = false;

/* Work item — defers bt_nus_send() out of NUS/BLE callback context */
static struct k_work notify_work;

/* TX buffer for JSON-encoded brake state */
static char tx_buf[128];

struct brake_state_json {
    char *message_type;
    int   data;
};

static const struct json_obj_descr brake_state_descr[] = {
    JSON_OBJ_DESCR_PRIM(struct brake_state_json, message_type, JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(struct brake_state_json, data,         JSON_TOK_NUMBER),
};

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_SRV_VAL),
};

static void do_notify(struct k_work *work)
{
    ARG_UNUSED(work);

    if (!notifications_enabled) {
        return;
    }

    struct brake_state_json state = {
        .message_type = "brake state",
        .data         = brake_get(),
    };

    int rc = json_obj_encode_buf(
                brake_state_descr,
                ARRAY_SIZE(brake_state_descr),
                &state,
                tx_buf,
                sizeof(tx_buf));
    if (rc != 0) {
        LOG_ERR("JSON encode failed [%d]", rc);
        return;
    }

    rc = bt_nus_send(NULL, tx_buf, strlen(tx_buf));
    if (rc < 0 && rc != -EAGAIN && rc != -ENOTCONN) {
        LOG_ERR("NUS send failed [%d]", rc);
    } else if (rc == 0) {
        LOG_INF("NUS sent: %s", tx_buf);
    }
}

static void on_nus_notif_enabled(bool enabled, void *ctx)
{
    ARG_UNUSED(ctx);
    notifications_enabled = enabled;
    LOG_INF("NUS notifications %s", enabled ? "enabled" : "disabled");

    /* Send an immediate state update when the central subscribes */
    if (enabled) {
        k_work_submit(&notify_work);
    }
}

static void on_nus_received(struct bt_conn *conn, const void *data,
                            uint16_t len, void *ctx)
{
    ARG_UNUSED(conn);
    ARG_UNUSED(ctx);

    /* Copy into a null-terminated buffer for handle_request() */
    char payload[256];
    if (len >= sizeof(payload)) {
        LOG_WRN("RX too large (%u bytes), truncating", len);
        len = sizeof(payload) - 1;
    }
    memcpy(payload, data, len);
    payload[len] = '\0';

    LOG_INF("NUS RX: %s", payload);
    (void)handle_request(payload, (size_t)len + 1);
}

static struct bt_nus_cb nus_cb = {
    .notif_enabled = on_nus_notif_enabled,
    .received      = on_nus_received,
};

static void mtu_exchange_cb(struct bt_conn *conn, uint8_t att_err,
                            struct bt_gatt_exchange_params *params)
{
    if (att_err == 0) {
        LOG_INF("MTU exchanged: %u", bt_gatt_get_mtu(conn));
    } else {
        LOG_WRN("MTU exchange failed (err %u)", att_err);
    }
}

static void on_connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        LOG_ERR("BLE connection failed (err %u)", err);
        return;
    }

    current_conn = bt_conn_ref(conn);
    ble_connected = true;
    LOG_INF("BLE central connected");

    /* Request a larger MTU so JSON payloads fit in one packet */
    mtu_params.func = mtu_exchange_cb;
    bt_gatt_exchange_mtu(conn, &mtu_params);
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOG_INF("BLE central disconnected (reason 0x%02x)", reason);

    if (current_conn) {
        bt_conn_unref(current_conn);
        current_conn = NULL;
    }

    ble_connected         = false;
    notifications_enabled = false;

    /* Restart advertising */
    int rc = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1,
                             ad, ARRAY_SIZE(ad),
                             sd, ARRAY_SIZE(sd));
    if (rc != 0) {
        LOG_ERR("Failed to restart advertising [%d]", rc);
    } else {
        LOG_INF("Advertising restarted");
    }
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected    = on_connected,
    .disconnected = on_disconnected,
};

int ble_service_notify_state(void)
{
    k_work_submit(&notify_work);
    return 0;
}

int ble_service_init(void)
{
    int rc;

    k_work_init(&notify_work, do_notify);

    /* Register NUS callbacks before enabling BT */
    rc = bt_nus_cb_register(&nus_cb, NULL);
    if (rc != 0) {
        LOG_ERR("Failed to register NUS callbacks [%d]", rc);
        return rc;
    }

    rc = bt_enable(NULL);
    if (rc != 0) {
        LOG_ERR("Bluetooth enable failed [%d]", rc);
        return rc;
    }
    LOG_INF("Bluetooth initialised");

    rc = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1,
                         ad, ARRAY_SIZE(ad),
                         sd, ARRAY_SIZE(sd));
    if (rc != 0) {
        LOG_ERR("Advertising start failed [%d]", rc);
        return rc;
    }

    LOG_INF("Advertising as \"%s\"", DEVICE_NAME);
    return 0;
}
