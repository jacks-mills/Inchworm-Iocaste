#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/data/json.h>
#include <string.h>

#include "request_handler.h"

LOG_MODULE_REGISTER(request_handler, LOG_LEVEL_INF);

K_MSGQ_DEFINE(requests, sizeof(struct brake_request), 10, 1);

/* JSON description of brake request and struct used to parse JSON*/
struct brake_request_json_format {
    char *message_type;
    char *sender;
    int percentage;
};

static const struct json_obj_descr brake_request_json[] = {
    JSON_OBJ_DESCR_PRIM(struct brake_request_json_format, message_type, JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(struct brake_request_json_format, sender, JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(struct brake_request_json_format, percentage, JSON_TOK_NUMBER),
};


int handle_request(char *request, size_t size) {
    struct brake_request_json_format format = { 0 };
    struct brake_request br = { 0 };
    size_t length = 0;
    int ret = 0;

    length = strnlen(request, size);

    /* attempt to parse json */
    ret = json_obj_parse(
            request, length,
            brake_request_json, ARRAY_SIZE(brake_request_json),
            &format);
    if (ret < 0) {
        LOG_WRN("json_obj_parse failed to parse %*s", length, request);
        return -1;
    }

    /* copy format to brake request struct */
    (void) strncpy(br.message_type, format.message_type, sizeof(br.message_type));
    if (br.message_type[sizeof(br.message_type) - 1] != '\0') {
        LOG_ERR("Request message type string too large");
        return -1;
    }

    (void) strncpy(br.sender, format.sender, sizeof(br.sender));
    if (br.sender[sizeof(br.sender) - 1] != '\0') {
        LOG_ERR("Request sender string too large");
        return -1;
    }

    br.percentage = format.percentage;

    /* log received message */
    LOG_INF("Request parsed: {message type: %s, sender: %s, percentage: %i}",
            format.message_type, format.sender, format.percentage);

    /* check message type */
    if (strcmp(format.message_type, "brake request") != 0) {
        LOG_WRN("Bad message type");
        return -1;
    } 

    /* attempt to queue request */
    ret = k_msgq_put(&requests, &br, K_NO_WAIT);
    if (ret != 0) {
        LOG_ERR("Failed to queue request");
        return -1;
    }

    return 0;
}
