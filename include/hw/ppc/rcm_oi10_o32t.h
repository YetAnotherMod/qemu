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
BusState *oi10_o32t_get_spi_bus(DeviceState *dev, int spi_num);

/**
 * @brief возвращает qemu_irq для линий INT0-INT7 которые служат для приема
 * прерывания от внешней микросхемы
 *
 * @param dev указатель на структуру устройства ОИ10/О32Т
 * @param int_num - номер линии внешнего прерывания. Разрешенные значения: 0-7
 * @return qemu_irq - линия для подключения прерывания от внешнего устройства
 */
qemu_irq oi10_o32t_get_ext_int_irq(DeviceState *dev, unsigned int_num);

#endif /* RCM_OI10_H */
