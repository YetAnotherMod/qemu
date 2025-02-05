#ifndef RCM_OI10_H
#define RCM_OI10_H

#include "qom/object.h"

#define TYPE_OI10 "oi10"
OBJECT_DECLARE_SIMPLE_TYPE(OI10State, OI10)

#define TYPE_O32T "o32t"
OBJECT_DECLARE_SIMPLE_TYPE(O32TState, O32T)

#define OI10_O32T_USE_INTERNAL_ROM 7
#define OI10_O32T_SD_CARD_INSERTED_GPIO 3
#define OI10_O32T_BOOT_IN_HOST_MODE 1
#define OI10_O32T_BOOT_CFG_DEFVAL \
    (1 << OI10_O32T_USE_INTERNAL_ROM | 1 << OI10_O32T_BOOT_IN_HOST_MODE)

MemoryRegion *oi10_o32t_get_ext_mem_region(DeviceState *dev);
BusState *oi10_o32t_get_sdio_bus(DeviceState *dev, int sdio_num);

#endif /* RCM_OI10_H */
