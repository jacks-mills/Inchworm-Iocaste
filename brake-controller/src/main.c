#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "ble_service.h"
#include "brake.h"
#include "request_handler.h"
#include "uart.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
    char tx_buf[MSG_SIZE] = { 0 };
    int ret = 0;

    printk("Starting in...\n");
    for (int i = 5; i > 0; i--) {
        printk("%i\n", i);
        k_sleep(K_MSEC(1000));
    }

    printk("BRAKE CONTROLLER\n");

    printk("Initialising brake\n");
    int error = brake_init();
    if (error) {
        printk("Brake init failed [error: %i]\n", error);
        return error;
    }
    brake_set(0);

    printk("Initialising UART\n");
    error = uart_init();
    if (error) {
        printk("UART init failed [error: %i]\n", error);
        return error;
    }

    printk("Initialising BLE\n");
    error = ble_service_init();
    if (error) {
        printk("BLE init failed [error: %i]\n", error);
        return error;
    }

    printk("Waiting for BLE connection…\n");

    struct brake_request request = { 0 };

    while (1) {
        if (k_msgq_get(&uart_msgq, &tx_buf, K_NO_WAIT) == 0) {
            printk("UART Received: %s\n", &tx_buf);
            error = handle_request(tx_buf, sizeof(tx_buf));
            if (error) {
                LOG_WRN("UART handle_request() failed");
            }
        }

        if (!ble_connected) {
            //printk("Not connected. Trying again\n");
            k_msleep(500);
            continue;
        }

        ret = k_msgq_get(&requests, &request, K_MSEC(600));
        if (ret != 0) {
            (void)ble_service_notify_state();
            continue;
        }

        printk("Received request from %s (percentage: %i)\n",
               request.sender, request.percentage);

        /* Apply the requested brake percentage and notify */
        (void)brake_set(request.percentage);
        (void)ble_service_notify_state();
    }

    return 0;
}
