#include <stdio.h>
#include "logger.h"

void info_log(char *message) {
    printf("[INFO]: %s\n", message);
}

void warn_log(char *message) {
    printf("[WARN]: %s\n", message);
}

void err_log(char *message) {
    printf("[ERROR]: %s\n", message);
}