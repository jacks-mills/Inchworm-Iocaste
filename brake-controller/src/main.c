/* source: https://hub.mender.io/t/connectivity-with-zephyr-part-1-wifi-on-esp32-s3/8130 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_event.h>

#include "wifi.h"

LOG_MODULE_REGISTER(wifi_app, LOG_LEVEL_INF);

int main(void)
{
    int error = 0;

    printk("BRAKE CONTROLLER\n");

    printk("Initialising wifi\n");
    wifi_init();

    printk("Connecting...\n");
    error = wifi_connect();
    if (error) {
        printk("Failed to connect\n");
        return 0;
    }

    printk("Get IP address\n");
    error = wifi_wait_for_ip_addr();
    if (error) {
        printk("IP address wait timed out\n");
        return 0;
    }

    printk("Disconnecting\n");
    (void) wifi_disconnect();

    return 0;
}
