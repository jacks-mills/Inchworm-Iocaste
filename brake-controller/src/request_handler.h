#ifndef __REQUEST_HANDLER__
#define __REQUEST_HANDLER__

#include <zephyr/kernel.h>

struct brake_request {
    char message_type[64];
    char sender[64];
    int percentage;
};

extern struct k_msgq requests;

int handle_request(char *request, size_t size);

#endif /* __REQUEST_HANDLER__ */
