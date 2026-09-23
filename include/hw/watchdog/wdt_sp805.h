/**
 * @file wdt_sp805.h
 * @author Нелюбин Виктор. ЗАО НТЦ Модуль (v.nelyubin@module.ru)
 * @brief эмулятор wdt sp805 для СнК ОИ10
 * @version 0.1
 * @date 2026-09-15
 * 
 * 
 */

#ifndef HW_WATCHDOG_SP805_H
#define HW_WATCHDOG_SP805_H

#include "qom/object.h"
#include "hw/irq.h"
#include "hw/ptimer.h"

/**
 * @brief Имя типа устройства SP805.
 */
#define TYPE_SP805 "sp805"

OBJECT_DECLARE_SIMPLE_TYPE(SP805State, SP805)

/**
 * @brief Состояние устройства SP805.
 */
struct SP805State
{
    /** @brief Родительский объект DeviceState. */
    DeviceState parent_obj;

    /** @brief Указатель на процессор (link-свойство "cpu-state"). */
    CPUState *cpu;

    /** @brief Базовая частота счётчика в герцах (свойство "base-freq"). */
    uint32_t base_freq;

    /** @brief Базовый адрес в DCR-пространстве (свойство "baseaddr"). */
    uint32_t baseaddr;

    /** @brief Внутренний таймер ptimer. */
    ptimer_state *timer;

    /** @brief Выход прерывания WDOGINT (именованный выход "wdt_irq"). */
    qemu_irq irq;

    /** @brief Выход сброса WDOGRES (именованный выход "wdt_res"). */
    qemu_irq reset;

    /** @brief Регистр загрузки WdogLoad. */
    uint32_t load;

    /** @brief Регистр управления WdogControl. */
    uint32_t control;

    /**
     * @brief Состояние блокировки WdogLock.
     *
     * 0 — запись во все регистры разрешена (unlocked),
     * 1 — запись во все регистры, кроме WdogLock, запрещена (locked).
     */
    uint32_t lock;

    /** @brief Регистр сырого статуса прерывания WdogRIS. */
    uint32_t ris;

    /** @brief Регистр маскированного статуса прерывания WdogMIS. */
    uint32_t mis;
};

#endif /* HW_WATCHDOG_SP805_H */