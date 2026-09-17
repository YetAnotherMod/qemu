#ifndef RCM_OI10_H
#define RCM_OI10_H

#include "qom/object.h"

#define TYPE_OI10 "oi10"
OBJECT_DECLARE_SIMPLE_TYPE(OI10State, OI10)

#define TYPE_O32T "o32t"
OBJECT_DECLARE_SIMPLE_TYPE(O32TState, O32T)

/**
    \defgroup OI10_INTERFACE_API
    \ingroup OI10
    \brief интерфейсные функции
*/
/**
    \defgroup OI10_PERIF_SETTINGS
    \ingroup OI10
    \brief настройки периферии СнК ОИ10
*/
/**
    \defgroup OI10_DCR_MEMORY_MAP
    \ingroup OI10
    \brief карта памяти  DCR СнК ОИ10
*/

/**
    \defgroup OI10_CONFIG
    \ingroup OI10
    \brief конфигурация сборки модели OI10
*/
#define OI10_O32T_USE_INTERNAL_ROM 7
#define OI10_O32T_SD_CARD_INSERTED_GPIO 3
#define OI10_O32T_BOOT_IN_HOST_MODE 1
#define OI10_O32T_BOOT_CFG_DEFVAL \
    (1 << OI10_O32T_USE_INTERNAL_ROM | 1 << OI10_O32T_BOOT_IN_HOST_MODE)


/// базовый адрес контроллера DIT по умолчанию. Можно изменять через свойство baseaddr
/// @ingroup OI10_DCR_MEMORY_MAP
#define DOUBLE_TIMER_BASE_ADDR     0x800A0000U
/// базовый адрес контроллера WDT по умолчанию. Можно изменять через свойство baseaddr
/// @ingroup OI10_DCR_MEMORY_MAP
#define WDT_BASE_ADDR              0x800B0000U

/// DIT IRQ line1
/// \ingroup OI10_PERIF_SETTINGS
#define OI10_DIT1_IRQ_LINE 40U
/// DIT IRQ line2
/// \ingroup OI10_PERIF_SETTINGS
#define OI10_DIT2_IRQ_LINE 41U
/// WDT IRQ line
/// \ingroup OI10_PERIF_SETTINGS
#define OI10_WDT_IRQ_LINE 42U

/**
* @brief базовая частота контроллера DIT
* @details Постоянная базовая частота контроллера. Подается от DCR(CLK_DCR)
* Эту частоту каждый таймер может делить на 1, 16, 256
* 
* @warning на данный момент от регистров системы тактирования не зависит и
* зафиксирована на значении 25MHz.
*
* @ingroup OI10_PERIF_SETTINGS
*/
#define DOUBLE_TIMER_BASE_FREQ  25000000

/**
* @brief базовая частота контроллера WDT
* @details Постоянная базовая частота контроллера. Подается от DCR(CLK_DCR)
* 
* @warning на данный момент от регистров системы тактирования не зависит и
* зафиксирована на значении 25MHz.
*
* @ingroup OI10_PERIF_SETTINGS
*/
#define WDT_BASE_FREQ  25000000
    

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
 *
 * @ingroup OI10_INTERFACE_API
 */
qemu_irq oi10_o32t_get_ext_int_irq(DeviceState *dev, unsigned int_num);

///@}
//<<<<<<<<<<<<<<<<oi10_interface_api defgroup end<<<<<<<<<<<<<<<<<<<<<<<

//>>>>>>>>>>>>>>>oi10_dcr_memory_map defgroup begin>>>>>>>>>>>>>>>>>>>>>>
/**
    \defgroup OI10_DCR_MEMORY_MAP
    \ingroup 
    \brief карта памяти  DCR СнК ОИ10
*/
///@{

/// базовый адрес контроллера DIT по умолчанию. Можно изменять через свойство baseaddr
#define DOUBLE_TIMER_BASE_ADDR   0x800A0000U

///@}
//<<<<<<<<<<<<<<<<oi10_dcr_memory_map defgroup end<<<<<<<<<<<<<<<<<<<<<<<


//>>>>>>>>>>>>>>>oi10_perif_settings defgroup begin>>>>>>>>>>>>>>>>>>>>>>
/**
    \defgroup OI10_PERIF_SETTINGS
    \ingroup //todo
    \brief настройки периферии СнК ОИ10
*/
///@{
///@}
//<<<<<<<<<<<<<<<<oi10_perif_settings defgroup end<<<<<<<<<<<<<<<<<<<<<<<

//>>>>>>>>>>>>>>>oi10_irq_lines defgroup begin>>>>>>>>>>>>>>>>>>>>>>
/**
    \defgroup OI10_IRQ_LINES
    \ingroup OI10_PERIF_SETTINGS
    \brief номера линий прерываний СнК ОИ10
*/
///@{

/// double timer1 IRQ line
#define OI10_DIT1_IRQ_LINE 40U
/// double timer2 IRQ line
#define OI10_DIT2_IRQ_LINE 41U

///@}
//<<<<<<<<<<<<<<<<oi10_irq_lines defgroup end<<<<<<<<<<<<<<<<<<<<<<<

//>>>>>>>>>>>>>>>dit_settings defgroup begin>>>>>>>>>>>>>>>>>>>>>>
/**
    \defgroup DIT_SETTINGS
    \ingroup OI10_PERIF_SETTINGS
    \brief настройки модуля DIT
*/
///@{

/// базовая частота контроллера DIT
/// @details Постоянная базовая частота контроллера. Подается от DCR(CLK_DCR)
/// Эту частоту каждый таймер может делить на 1, 16, 256
/// @warning на данный момент от регистров системы тактирования не зависит и
/// зафиксировано на значении 25MHz.
#define DOUBLE_TIMER_BASE_FREQ  25000000

///@}
//<<<<<<<<<<<<<<<<dit_settings defgroup end<<<<<<<<<<<<<<<<<<<<<<<

#endif /* RCM_OI10_H */
