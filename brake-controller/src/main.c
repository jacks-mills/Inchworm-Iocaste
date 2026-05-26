/* source: https://hub.mender.io/t/connectivity-with-zephyr-part-1-wifi-on-esp32-s3/8130 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mqtt.h>

#include "mqtt_client.h"
#include "wifi.h"
#include "brake.h"
#include "request_handler.h"

LOG_MODULE_REGISTER(wifi_app, LOG_LEVEL_INF);

#define MQTT_THREAD_STACK_SIZE 2048
#define MQTT_THREAD_PRIORITY   5

K_THREAD_STACK_DEFINE(mqtt_stack, MQTT_THREAD_STACK_SIZE);
static struct k_thread mqtt_thread;




/* MQTT client struct */
static struct mqtt_client client_ctx;


int main(void)
{
    int error = 0, ret = 0;;

    printk("BRAKE CONTROLLER\n");

    printk("Initialising brake\n");
    error = brake_init();
    if (error) {
        printk("Brake init failed [error: %i]\n", error);
        return error;
    }

    return 0;

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

    printk("Spawning MQTT thread\n");
    k_thread_create(&mqtt_thread, mqtt_stack,
                K_THREAD_STACK_SIZEOF(mqtt_stack),
                (k_thread_entry_t) app_mqtt_run,
                &client_ctx, NULL, NULL,
                MQTT_THREAD_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&mqtt_thread, "mqtt_run");

    struct brake_request request = { 0 };
    while (1) {
        if (mqtt_connected == false) {
            printk("Disconnected from broker :^(\n");
            break;
        }

        /* get next request (if there is one) */
        ret = k_msgq_get(&requests, &request, K_MSEC(5000));
        if (ret != 0) {
            printk("No messsaged\n");
            continue;
        }

        printk("Received request from %s (percentage: %i)\n",
                request.sender, request.percentage);

        /* set brake percentage in request */
        (void) brake_set(request.percentage);
        k_sem_give(&mqtt_publish_req); /* request publish */
    }

    return 0;
}
