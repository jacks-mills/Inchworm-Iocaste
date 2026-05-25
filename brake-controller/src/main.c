/* source: https://hub.mender.io/t/connectivity-with-zephyr-part-1-wifi-on-esp32-s3/8130 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mqtt.h>

#include "mqtt_client.h"
#include "wifi.h"
#include "brake.h"

LOG_MODULE_REGISTER(wifi_app, LOG_LEVEL_INF);

/* MQTT client struct */
static struct mqtt_client client_ctx;


int main(void)
{
    int error = 0;

    printk("BRAKE CONTROLLER\n");

    printk("Initialising brake\n");
    error = brake_init();
    if (error) {
        printk("Brake init failed [error: %i]\n", error);
        return error;
    }

    brake_set(0);

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

    printk("Subscribing to MQTT topics\n");
    error = app_mqtt_subscribe(&client_ctx);
	if (error) {
        printk("MQTT failed to subscribe [error: %i]\n", error);
        return error;
	}


    int percentage = 0;
    while (1) {
        percentage++;
        percentage = percentage % 100;

        printk("sp: %i\n", percentage);
        printk("bp: %i\n", brake_set(percentage));
        (void) app_mqtt_publish(&client_ctx);

        k_sleep(K_MSEC(300));
    }

    return 0;
}
