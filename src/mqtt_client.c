#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_mqtt, LOG_LEVEL_DBG);

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/data/json.h>
#include <zephyr/random/random.h>

static uint8_t rx_buffer[];
static uint8_t tx_buffer[];

static uint8_t payload_buf[];

static struct sockaddr_storage broker;

static struct zsock_pollfd fds[1];
static int nfds;

static const struct json_obj_descr sensor_sample_descr[];

bool mqtt_connected;

static uint8_t client_id[50];

void app_mqtt_connect(struct mqtt_client *client)
{
    int rc = 0;

    // Marks the client as disconnected before trying to connect
    mqtt_connected = false;

    // Keeps tyring until broker accepts connection
    while (!mqtt_connected) {

        // Initiates TCP connection, optional TLS handshake and 
        // MQTT CONNECT packet transmission
        rc = mqtt_connect(client);

        // Connection attempt failed immediately
        if (rc != 0) {

            // Logs error code
            LOG_ERR("MQTT Connect failed [%d]", rc);

            // Sleeps before retrying
            k_msleep(MSECS_WAIT_RECONNECT);
            continue;
        }

        // Waits for data on MQTT socket, CONNACK packet arrives asynchronously
        rc = poll_mqtt_socket(client, MSECS_NET_POLL_TIMEOUT);

        // Socket has incoming data
        if (rc > 0) {
            // Parses received MQTT packets 
            mqtt_input(client);
        }

        // Even after processing socket data broker may reject connection,
        // timeout may occur or malformed response may happen 
        if (!mqtt_connected) {
            // Forcefully close the MQTT socket and internal connection state
            mqtt_abort(client);
        }
    }
}

int app_mqtt_init(struct mqtt_client *client)
{
    int rc;

    //IP address of broker
    uint8_t broker_ip[NET_IPV4_ADDR_LEN];

    //IPv4 socket address
    struct sockaddr_in *broker4;

    //DNS lookup result
    struct addrinfo *result;

    //DNS Lookup Hints
    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM
    };

    // Resolve IP address of MQTT broker
    rc = getaddrinfo(CONFIG_NET_SAMPLE_MQTT_BROKER_HOSTNAME,
        CONFIG_NET_SAMPLE_MQTT_BROKER_PORT, &hints, &result);
    
    // DNS lookup failed
    if (rc != 0) {
        // Turn error codes into readable strings
        LOG_ERR("Failed to resolve broker hostname [%s]", gai_strerror(rc));
        return -EIO;
    }

    // Even if an address was retrieved, check if address is not empty
    if (result == NULL) {
        LOG_ERR("Broker address not found");
        return -ENOENT;
    }
    
    // Convert generic storage into IPv4-specific storage
    broker4 = (struct sockaddr_in *)&broker;

    // Copies broker IP address into global broker structure
    broker4->sin_addr.s_addr = ((struct sockaddr_in *)result->ai_addr)->sin_addr.s_addr;

    // Set address family to IPv4
    broker4->sin_family = AF_INET;

    // Copies resolved TCP port
    broker4->sin_port = ((struct sockaddr_in *)result->ai_addr)->sin_port;

    // Free DNS results
    freeaddrinfo(result);

    // Convert binary IP into human-readable string
    inet_ntop(AF_INET, &broket4->sin_addr.s_addr, broker_ip, sizeof(broker_ip));
    LOG_INF("Connecting to MQTT broker @ %s", broker_ip);

    // Generates a unique client ID
    init_mqtt_client_id();

    // Initializes Zephyr MQTT client internals
    mqtt_client_init(client);

    // Informs MQTT library where to connect
    client->broker = &broker;

    /* Resgisters the MQTT event handler, function handles connect events, 
        publish events, disconnects and acknowledgements */
    client->evt_cb = mqtt_event_handler;

    // Configure client id
    client->client_id.utf8 = client_id;
    client->client_id.size = strlen(client->client_id.utf8);

    // Set authenatication 
    client->password = NULL;
    client->user_name = NULL;

    // Use MQTT 3.1.1 protocol
    client->protocol_version = MQTT_VERSION_3_1_1;

    // Configure receive buffer
    client->rx_buf = rx_buffer;
    client->rx_buf_size = sizeof(rx_buffer);

    // Configure transmit buffer
    client->tx_buf = tx_buffer;
    client->tx_buf_size = sizeof(tx_buffer);

    #if defined(CONFIG_MQTT_LIB_TLS)
        struct mqtt_sec_config *tls_config;

        client->transport.type = MQTT_TRANSPORT_SECURE;

        rc = tls_init();
        if (rc != 0) {
            LOG_ERR("TLS init error");
            return rc;
        }

        tls_config = &client->transport.tls.config;
        tls_config->peer_verify = TLS_PEER_VERIFY_REQUIRED;
        tls_config->cipher_list = NULL;
        tls_config->sec_tag_list = m_sec_tags;
        tls_config->sec_tag_count = ARRAY_SIZE(m_sec_tags);

        #if defined(CONFIG_MBEDTLS_SERVER_NAME_INDICATION)
            tls_config->hostname = TLS_SNI_HOSTNAME;
        #else
            tls_config->hostname = NULL;
        #endif
    #endif 
    return rc;
}