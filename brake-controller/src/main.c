/* source: https://hub.mender.io/t/connectivity-with-zephyr-part-1-wifi-on-esp32-s3/8130 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mqtt.h>

#include "wifi.h"
#include "mqtt_client.h"

LOG_MODULE_REGISTER(wifi_app, LOG_LEVEL_INF);

/* MQTT client struct */
static struct mqtt_client client_ctx;

/* MQTT publish work item */
//struct k_work_delayable mqtt_publish_work;

//static struct net_mgmt_event_callback net_l4_mgmt_cb;

/* Network connection semaphore */
//K_SEM_DEFINE(net_conn_sem, 0, 1);

int main(void)
{
    int error = 0;

    printk("BRAKE CONTROLLER\n");

    printk("Initialising wifi\n");
    wifi_init();

    printk("Connecting...\n");
    error = wifi_connect();
    if (error) {
        printk("Failed to connect [error: %i]\n", error);
        return error;
    }

    printk("Get IP address\n");
    error = wifi_wait_for_ip_addr();
    if (error) {
        printk("IP address wait timed out [error: %i]\n", error);
        return error;
    }

    printk("Initialising MQTT\n");
	error = app_mqtt_init(&client_ctx);
	if (error) {
        printk("MQTT failed to initialise [error: %i]\n", error);
        return error;
	}

    printk("Connecting to broker\n");
	app_mqtt_connect(&client_ctx); /* block until MQTT connection is up */

    printk("Connected!\n");

    while (1) {
        k_sleep(K_MSEC(1000));
        printk("God I love busy waiting\n");
    }

    return 0;
}
