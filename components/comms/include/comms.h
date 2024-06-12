#ifndef COMMS_H
#define COMMS_H

#include "mqtt_client.h"

void mqtt_app_start(esp_mqtt_event_handle_t mqtt_event_handler_cb);

#endif // COMMS_H
