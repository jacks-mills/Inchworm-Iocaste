#ifndef __BLE_SERVICE_H__
#define __BLE_SERVICE_H__

#include <stdbool.h>

int ble_service_init(void);

int ble_service_notify_state(void);

extern volatile bool ble_connected; /* true while a central is connected. */

#endif /* __BLE_SERVICE_H__ */
