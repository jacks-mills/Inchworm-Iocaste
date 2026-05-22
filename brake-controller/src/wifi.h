#ifndef _WIFI_H
#define _WIFI_H

void wifi_init(void);
int wifi_connect(void);
int wifi_wait_for_ip_addr(void);
int wifi_disconnect(void);

#endif /* _WIFI_H */
