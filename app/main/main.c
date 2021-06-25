/*
    Radgard Main
*/

#include <stdio.h>

#include "server.h"

void app_main(void) {
    server_start_provisioning_or_connect_wifi();
    // Now connected to the internet
}
