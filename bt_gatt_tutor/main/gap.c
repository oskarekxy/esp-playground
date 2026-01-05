#include "gap.h"

// Private variables //
static uint8_t mac_addr_type;     // address type ( public, random static, random private resolvable, random private non-resolvable )
static uint8_t mac_addr[6] = {0}; // 48 bit mac address