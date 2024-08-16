#include <unistd.h>
#include "util.h"

// Sleeps for some amount of ms
void sleep_ms(int ms) {
    usleep(ms * 1000);
}
