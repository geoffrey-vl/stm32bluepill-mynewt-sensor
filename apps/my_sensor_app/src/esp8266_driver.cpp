//  ESP8266 driver for Apache Mynewt
#include <os/os.h>
#include <sensor/sensor.h>
#include "ESP8266.h"
#include "esp8266_driver.h"

//  Use static buffers to avoid dynamic memory allocation (new, delete)
#define ESP8266_TX_BUFFER_SIZE 256
#define ESP8266_RX_BUFFER_SIZE 256
#define ESP8266_PARSER_BUFFER_SIZE 256

//  Various timeouts for different ESP8266 operations
#define ESP8266_CONNECT_TIMEOUT 15000
#define ESP8266_SEND_TIMEOUT    500
#define ESP8266_RECV_TIMEOUT    0
#define ESP8266_MISC_TIMEOUT    500

static int config_esp8266(void);
static int esp8266_init(struct os_dev *dev0, void *arg);

static char esp8266_tx_buffer[ESP8266_TX_BUFFER_SIZE];  //  TX Buffer
static char esp8266_rx_buffer[ESP8266_RX_BUFFER_SIZE];  //  RX Buffer
static char esp8266_parser_buffer[ESP8266_PARSER_BUFFER_SIZE];  //  Buffer for ATParser

//  #if MYNEWT_VAL(UART_0) && MYNEWT_VAL(ESP8266_OFB)
static ESP8266 driver;  //  The single C++ driver instance.  TODO: Support multiple ESP8266 instances.
static struct esp8266 esp8266;  //  Mynewt driver instance.

static const struct sensor_itf uart_0_itf = {        
    SENSOR_ITF_UART, //  si_type: Sensor interface type
    0,               //  si_num: Sensor interface number    
};
//  #endif  //  MYNEWT_VAL(UART_0) && MYNEWT_VAL(ESP8266_OFB)

#ifdef NOTUSED
#define COAP_PORT_UNSECURED (5683)

typedef struct {
    uint8_t address[4];
} oc_ipv4_addr_t;

/*
 * oc_endpoint for IPv4/IPv6
 */
struct oc_endpoint_ip {
    struct oc_ep_hdr ep;
    uint16_t port;
    oc_ipv4_addr_t v4;
};

static inline int oc_endpoint_is_ip(struct oc_endpoint *oe) {
    return oe->ep.oe_type == oc_ip6_transport_id ||
      oe->ep.oe_type == oc_ip4_transport_id;
}

#define oc_make_ip4_endpoint(__name__, __flags__, __port__, ...)        \
    struct oc_endpoint_ip __name__ = {.ep = {.oe_type = oc_ip4_transport_id, \
                                             .oe_flags = __flags__},    \
                                      .port = __port__,                 \
                                      .v4 = {.address = { __VA_ARGS__ } } }

static void oc_send_buffer_ip4(struct os_mbuf *m);
static void oc_send_buffer_ip4_mcast(struct os_mbuf *m);
static uint8_t oc_ep_ip4_size(const struct oc_endpoint *oe);
static char *oc_log_ep_ip4(char *ptr, int maxlen, const struct oc_endpoint *);
static int oc_connectivity_init_ip4(void);
void oc_connectivity_shutdown_ip4(void);
static void oc_event_ip4(struct os_event *ev);

static const struct oc_transport oc_ip4_transport = {
    .ot_flags = 0,
    .ot_ep_size = oc_ep_ip4_size,
    .ot_tx_ucast = oc_send_buffer_ip4,
    .ot_tx_mcast = oc_send_buffer_ip4_mcast,
    .ot_get_trans_security = NULL,
    .ot_ep_str = oc_log_ep_ip4,
    .ot_init = oc_connectivity_init_ip4,
    .ot_shutdown = oc_connectivity_shutdown_ip4
};

uint8_t oc_ip4_transport_id = -1;

void oc_register_ip4(void) {
#if (MYNEWT_VAL(OC_TRANSPORT_IP) == 1) && (MYNEWT_VAL(OC_TRANSPORT_IPV4) == 1)
    oc_ip4_transport_id = oc_transport_register(&oc_ip4_transport);
#endif
}
#endif

void esp8266_sensor_dev_create(void) {  //  TODO: Rename.
    //  Create the ESP8266 device, configure it and register with Sensor Manager.  Called by main().
    int rc;
    //  Create the device and register with Sensor Manager.
    rc = os_dev_create((struct os_dev *) &esp8266, ESP8266_DEVICE,
        OS_DEV_INIT_PRIMARY, 0, esp8266_init, (void *) &uart_0_itf);
    assert(rc == 0);

    //  Configure the device.
    rc = config_esp8266();
    assert(rc == 0);
}

static int config_esp8266(void) {
    //  Fetch the ESP8266 device from Mynewt, configure it and register with Sensor Manager.
    int rc;
    struct os_dev *dev;
    struct esp8266_cfg cfg = {};
    dev = (struct os_dev *) os_dev_open(ESP8266_DEVICE, OS_TIMEOUT_NEVER, NULL);
    assert(dev != NULL);

    //  Configure the device.
    rc = esp8266_config((struct esp8266 *) dev, &cfg);
    os_dev_close(dev);
    return rc;
}

/////////////////////////////////////////////////////////
//  Sensor Creator Tasks

static void esp8266_event(void *drv);
static int internal_connect(struct sensor_itf *itf);

//  Exports for the sensor API
static int esp8266_sensor_read(struct sensor *, sensor_type_t,
        sensor_data_func_t, void *, uint32_t) { return 0; }
static int esp8266_sensor_get_config(struct sensor *, sensor_type_t type,
        struct sensor_cfg *cfg) {
    //  int rc;
    //  if (!(type & SENSOR_TYPE_PRESSURE)) { rc = SYS_EINVAL; goto err; }
    cfg->sc_valtype = SENSOR_VALUE_TYPE_FLOAT;
    return (0);
    //  err: return (rc);
}

//  ESP8266 Mynewt Sensor Driver
static const struct sensor_driver g_esp8266_sensor_driver = {
    esp8266_sensor_read,
    esp8266_sensor_get_config
};

static int esp8266_default_cfg(struct esp8266_cfg *cfg) {
    //  cfg->bc_iir = BME280_FILTER_OFF;
    return 0;
}

static int esp8266_init(struct os_dev *dev0, void *arg) {
    //  Configure the device and register with Sensor Manager.  Called by os_dev_create().
    struct esp8266 *dev;
    struct sensor *sensor;
    int rc;
    if (!arg || !dev0) { rc = SYS_ENODEV; goto err; }

    dev = (struct esp8266 *) dev0;
    rc = esp8266_default_cfg(&dev->cfg);
    if (rc) { goto err; }

    sensor = &dev->sensor;
#ifdef NOTUSED
    /* Initialise the stats entry */
    rc = stats_init(
        STATS_HDR(g_bme280stats),
        STATS_SIZE_INIT_PARMS(g_bme280stats, STATS_SIZE_32),
        STATS_NAME_INIT_PARMS(bme280_stat_section));
    SYSINIT_PANIC_ASSERT(rc == 0);
    /* Register the entry with the stats registry */
    rc = stats_register(dev->od_name, STATS_HDR(g_bme280stats));
    SYSINIT_PANIC_ASSERT(rc == 0);
#endif  //  NOTUSED
    rc = sensor_init(sensor, dev0);
    if (rc != 0) { goto err; }

    //  Add the driver with all the supported types.
    rc = sensor_set_driver(sensor, SENSOR_TYPE_NONE,
                           (struct sensor_driver *) &g_esp8266_sensor_driver);
    if (rc != 0) { goto err; }

    //  Set the driver interface.
    rc = sensor_set_interface(sensor, (sensor_itf *) arg);
    if (rc) { goto err; }

    //  Register with Sensor Manager.
    rc = sensor_mgr_register(sensor);
    if (rc != 0) { goto err; }
    return (0);
err:
    return (rc);
}

int esp8266_config(struct esp8266 *drv, struct esp8266_cfg *cfg) {
    //  Configure the ESP8266 driver.
    memset(cfg->_ids, 0, sizeof(cfg->_ids));
    memset(cfg->_cbs, 0, sizeof(cfg->_cbs));
    driver.init(  //  Set the buffers for the C++ instance. We pass in static buffers to avoid dynamic memory allocation (new, delete).
        esp8266_tx_buffer, ESP8266_TX_BUFFER_SIZE,
        esp8266_rx_buffer, ESP8266_RX_BUFFER_SIZE,
        esp8266_parser_buffer, ESP8266_PARSER_BUFFER_SIZE
    );
    driver.configure(drv->sensor.s_itf.si_num);  //  Configure the UART port.  0 means UART2.
    driver.attach(&esp8266_event, drv);          //  Set the callback for ESP8266 events.
    return 0;
}

/////////////////////////////////////////////////////////
//  ESP8266 Driver Interface based on https://os.mbed.com/teams/ESP8266/code/esp8266-driver/file/6946b0b9e323/ESP8266Interface.cpp/

static ESP8266 *drv(struct sensor_itf *itf) { return &driver; }  //  TODO: Return the ESP8266 Driver based on itf.si_num
static esp8266_cfg *cfg(struct sensor_itf *itf) { return &esp8266.cfg; }  //  TODO: Return the ESP8266 Config based on itf.si_num

int esp8266_scan(struct sensor_itf *itf, nsapi_wifi_ap_t *res, unsigned limit) {
    //  Scan for WiFi access points.
    drv(itf)->setTimeout(ESP8266_CONNECT_TIMEOUT);
    if (!drv(itf)->startup(3)) { return NSAPI_ERROR_DEVICE_ERROR; }  //  Start in WiFi Client mode.
    return drv(itf)->scan(res, limit);
}

int esp8266_connect(struct sensor_itf *itf, const char *ssid, const char *pass, nsapi_security_t security, uint8_t channel) {
    if (channel != 0) { return NSAPI_ERROR_UNSUPPORTED; }
    esp8266_set_credentials(itf, ssid, pass, security);
    return internal_connect(itf);
}

static int internal_connect(struct sensor_itf *itf) {
    drv(itf)->setTimeout(ESP8266_CONNECT_TIMEOUT);
    if (!drv(itf)->startup(3)) { return NSAPI_ERROR_DEVICE_ERROR; }
    if (!drv(itf)->dhcp(true, 1)) { return NSAPI_ERROR_DHCP_FAILURE; }
    if (!drv(itf)->connect(cfg(itf)->ap_ssid, cfg(itf)->ap_pass)) { return NSAPI_ERROR_NO_CONNECTION; }
    if (!drv(itf)->getIPAddress()) { return NSAPI_ERROR_DHCP_FAILURE; }
    return NSAPI_ERROR_OK;
}

int esp8266_set_credentials(struct sensor_itf *itf, const char *ssid, const char *pass, nsapi_security_t security) {
    memset(cfg(itf)->ap_ssid, 0, sizeof(cfg(itf)->ap_ssid));
    strncpy(cfg(itf)->ap_ssid, ssid, sizeof(cfg(itf)->ap_ssid));
    memset(cfg(itf)->ap_pass, 0, sizeof(cfg(itf)->ap_pass));
    strncpy(cfg(itf)->ap_pass, pass, sizeof(cfg(itf)->ap_pass));
    cfg(itf)->ap_sec = security;
    return 0;
}

static void esp8266_event(void *drv) {
    //  Callback for ESP8266 events.
#ifdef TODO
    for (int i = 0; i < ESP8266_SOCKET_COUNT; i++) {
        if (_cbs[i].callback) {
            _cbs[i].callback(_cbs[i].data);
        }
    }
#endif  //  TODO
}

int esp8266_disconnect(struct sensor_itf *itf) {
    drv(itf)->setTimeout(ESP8266_MISC_TIMEOUT);
    if (!drv(itf)->disconnect()) {
        return NSAPI_ERROR_DEVICE_ERROR;
    }
    return NSAPI_ERROR_OK;
}

const char *esp8266_get_ip_address(struct sensor_itf *itf) {
    return drv(itf)->getIPAddress();
}

const char *esp8266_get_mac_address(struct sensor_itf *itf) {
    return drv(itf)->getMACAddress();
}

const char *esp8266_get_gateway(struct sensor_itf *itf) {
    return drv(itf)->getGateway();
}

const char *esp8266_get_netmask(struct sensor_itf *itf) {
    return drv(itf)->getNetmask();
}

int8_t esp8266_get_rssi(struct sensor_itf *itf) {
    return drv(itf)->getRSSI();
}

struct esp8266_socket {
    int id;
    nsapi_protocol_t proto;
    bool connected;
    const char *host;
    uint16_t port;
};

int esp8266_socket_open(struct sensor_itf *itf, void **handle, nsapi_protocol_t proto) {
    // Look for an unused socket
    int id = -1;
    for (int i = 0; i < ESP8266_SOCKET_COUNT; i++) {
        if (!cfg(itf)->_ids[i]) {
            id = i;
            cfg(itf)->_ids[i] = true;
            break;
        }
    }
    if (id == -1) { return NSAPI_ERROR_NO_SOCKET; }    
    struct esp8266_socket *socket = new struct esp8266_socket;
    if (!socket) { return NSAPI_ERROR_NO_SOCKET; }
    socket->id = id;
    socket->proto = proto;
    socket->connected = false;
    *handle = socket;
    return 0;
}

int esp8266_socket_close(struct sensor_itf *itf, void *handle) {
    struct esp8266_socket *socket = (struct esp8266_socket *)handle;
    int err = 0;
    drv(itf)->setTimeout(ESP8266_MISC_TIMEOUT);
    if (!drv(itf)->close(socket->id)) { err = NSAPI_ERROR_DEVICE_ERROR; }
    cfg(itf)->_ids[socket->id] = false;
    delete socket;
    return err;
}

int esp8266_socket_connect(struct sensor_itf *itf, void *handle, const char *host, uint16_t port) {
    struct esp8266_socket *socket = (struct esp8266_socket *)handle;
    drv(itf)->setTimeout(ESP8266_MISC_TIMEOUT);
    const char *proto = (socket->proto == NSAPI_UDP) ? "UDP" : "TCP";
    if (!drv(itf)->open(proto, socket->id, host, port)) {
        return NSAPI_ERROR_DEVICE_ERROR;
    }
    socket->connected = true;
    return 0;
}

int esp8266_socket_send(struct sensor_itf *itf, void *handle, const void *data, unsigned size) {
    struct esp8266_socket *socket = (struct esp8266_socket *)handle;
    drv(itf)->setTimeout(ESP8266_SEND_TIMEOUT);
    if (!drv(itf)->send(socket->id, data, size)) {
        return NSAPI_ERROR_DEVICE_ERROR;
    }
    return size;
}

int esp8266_socket_sendto(struct sensor_itf *itf, void *handle, const char *host, uint16_t port, const void *data, unsigned size) {
    //  Note: Host must point to a static string that will never change.
    struct esp8266_socket *socket = (struct esp8266_socket *)handle;
    if (socket->connected && (socket->host != host || socket->port != port)) {  //  If connected but sending to a different destination...
        drv(itf)->setTimeout(ESP8266_MISC_TIMEOUT);
        if (!drv(itf)->close(socket->id)) { return NSAPI_ERROR_DEVICE_ERROR; }
        socket->connected = false;
    }
    if (!socket->connected) {
        int err = esp8266_socket_connect(itf, socket, host, port);
        if (err < 0) { return err; }
        socket->host = host;
        socket->port = port;
    }
    return esp8266_socket_send(itf, socket, data, size);
}

void esp8266_socket_attach(struct sensor_itf *itf, void *handle, void (*callback)(void *), void *data) {
    struct esp8266_socket *socket = (struct esp8266_socket *)handle;    
    cfg(itf)->_cbs[socket->id].callback = callback;
    cfg(itf)->_cbs[socket->id].data = data;
}

#ifdef NOTUSED

    int esp8266_socket_bind(void *handle, const SocketAddress &address)
    {
        return NSAPI_ERROR_UNSUPPORTED;
    }

    int esp8266_socket_listen(void *handle, int backlog)
    {
        return NSAPI_ERROR_UNSUPPORTED;
    }
        
    int esp8266_socket_accept(void *server, void **socket, SocketAddress *addr)
    {
        return NSAPI_ERROR_UNSUPPORTED;
    }

    int esp8266_socket_recv(void *handle, void *data, unsigned size)
    {
        struct esp8266_socket *socket = (struct esp8266_socket *)handle;
        drv(itf)->setTimeout(ESP8266_RECV_TIMEOUT);
    
        int32_t recv = drv(itf)->recv(socket->id, data, size);
        if (recv < 0) {
            return NSAPI_ERROR_WOULD_BLOCK;
        }
    
        return recv;
    }

    int esp8266_socket_recvfrom(void *handle, SocketAddress *addr, void *data, unsigned size)
    {
        struct esp8266_socket *socket = (struct esp8266_socket *)handle;
        int ret = socket_recv(socket, data, size);
        if (ret >= 0 && addr) {
            *addr = socket->addr;
        }

        return ret;
    }

    void esp8266_event() {
        for (int i = 0; i < ESP8266_SOCKET_COUNT; i++) {
            if (_cbs[i].callback) {
                _cbs[i].callback(_cbs[i].data);
            }
        }
    }

#endif  //  NOTUSED