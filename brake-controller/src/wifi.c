#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_event.h>

#include "wifi.h"


static struct net_mgmt_event_callback wifi_cb;
static struct net_mgmt_event_callback ipv4_cb;

static K_SEM_DEFINE(sem_wifi, 0, 1);
static K_SEM_DEFINE(sem_ipv4, 0, 1);

LOG_MODULE_REGISTER(wifi, CONFIG_LOG_DEFAULT_LEVEL);

static void wifi_event_handler(struct net_mgmt_event_callback *cb,
                               uint64_t mgmt_event, struct net_if *iface)
{
    const struct wifi_status *status = (const struct wifi_status *)cb->info;

    if (mgmt_event == NET_EVENT_WIFI_CONNECT_RESULT) {
        if (status->conn_status) {
            LOG_ERR("WiFi connection failed: %s (%d)",
                    wifi_conn_status_txt(status->conn_status),
                    status->conn_status);
        } else {
            LOG_INF("WiFi connected");
            k_sem_give(&sem_wifi);
        }
    } else if (mgmt_event == NET_EVENT_WIFI_DISCONNECT_RESULT) {
        LOG_INF("WiFi disconnected, reason: %d", status->disconn_reason);
        k_sem_take(&sem_wifi, K_NO_WAIT);
    }
}

static void ipv4_event_handler(struct net_mgmt_event_callback *cb,
                               uint64_t mgmt_event, struct net_if *iface)
{
    if (mgmt_event == NET_EVENT_IPV4_ADDR_ADD) {
        k_sem_give(&sem_ipv4);

//        struct net_if_ipv4 *ipv4 = iface->config.ip.ipv4;
//
//        if (ipv4) {
//            char addr_str[NET_IPV4_ADDR_LEN];
//
//            for (int i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
//                if (ipv4->unicast[i].ipv4.is_used) {
//                    net_addr_ntop(AF_INET,
//                                  &ipv4->unicast[i].ipv4.address.in_addr,
//                                  addr_str, sizeof(addr_str));
//                    LOG_INF("IP Address: %s", addr_str);
//                }
//            }
//        }
    }
}


void wifi_init(void) {
    net_mgmt_init_event_callback(&wifi_cb, wifi_event_handler,
                                 NET_EVENT_WIFI_CONNECT_RESULT |
                                 NET_EVENT_WIFI_DISCONNECT_RESULT);
    net_mgmt_add_event_callback(&wifi_cb);

    net_mgmt_init_event_callback(&ipv4_cb, ipv4_event_handler,
                                 NET_EVENT_IPV4_ADDR_ADD);
    net_mgmt_add_event_callback(&ipv4_cb);
}

int wifi_connect(void) {
    int error = 0;
    struct net_if *iface = NULL;
    struct wifi_connect_req_params params = { 0 };

    iface = net_if_get_default();
    if (iface == NULL) {
        return -1;
    }

    params.ssid = CONFIG_WIFI_SSID,
    params.ssid_length = strlen(CONFIG_WIFI_SSID),
    params.psk = CONFIG_WIFI_PSK,
    params.psk_length = strlen(CONFIG_WIFI_PSK),
    params.channel = WIFI_CHANNEL_ANY,
    params.band = WIFI_FREQ_BAND_2_4_GHZ,
    params.security = WIFI_SECURITY_TYPE_PSK,
    params.mfp = WIFI_MFP_OPTIONAL;

    error = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params, sizeof(params));
    if (error) {
        return -1;
    }

    /* wait 10 seconds */
    error = k_sem_take(&sem_wifi, K_MSEC(10000));
    if (error) {
        return -1;
    }

    return 0;
}

int wifi_wait_for_ip_addr(void) {
    struct wifi_iface_status status = { 0 };
    struct net_if *iface = NULL;
    char ip_addr[NET_IPV4_ADDR_LEN] = { 0 };
    char gw_addr[NET_IPV4_ADDR_LEN] = { 0 };
    int error = 0;

    // Get interface
    iface = net_if_get_default();
    if (iface == NULL) {
        return -1;
    }

    /* wait 10 seconds */
    error = k_sem_take(&sem_ipv4, K_MSEC(10000));
    if (error) {
        return -1;
    }

    // Get the WiFi status
    if (net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS,
                 iface,
                 &status,
                 sizeof(struct wifi_iface_status))) {
        printk("Error: WiFi status request failed\n");
        return -1;
    }

    // Get the IP address
    memset(ip_addr, 0, sizeof(ip_addr));
    if (net_addr_ntop(AF_INET,
                      &iface->config.ip.ipv4->unicast[0].ipv4.address.in_addr,
                      ip_addr,
                      sizeof(ip_addr)) == NULL) {
        printk("Error: Could not convert IP address to string\n");
        return -1;
    }

    // Get the gateway address
    memset(gw_addr, 0, sizeof(gw_addr));
    if (net_addr_ntop(AF_INET,
                      &iface->config.ip.ipv4->gw,
                      gw_addr,
                      sizeof(gw_addr)) == NULL) {
        printk("Error: Could not convert gateway address to string\n");
        return -1;
    }

    // Print the WiFi status
    printk("WiFi status:\n");
    if (status.state >= WIFI_STATE_ASSOCIATED) {
        printk("  SSID: %-32s\n", status.ssid);
        printk("  Band: %s\n", wifi_band_txt(status.band));
        printk("  Channel: %d\n", status.channel);
        printk("  Security: %s\n", wifi_security_txt(status.security));
        printk("  IP address: %s\n", ip_addr);
        printk("  Gateway: %s\n", gw_addr);
    }

    return 0;
}

int wifi_disconnect(void) {
    return net_mgmt(
            NET_REQUEST_WIFI_DISCONNECT,
            net_if_get_default(),
            NULL,
            0);
}
