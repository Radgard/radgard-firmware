/*
    Radgard Main
*/

#include <stdio.h>

#include "network.h"
#include "api.h"

void app_main(void) {
    network_start_provision_connect_wifi();
    // Now connected to the internet

    api_get_irrigation_settings();
}
