/**
 * @file wdt_sp805.c
 * @author Нелюбин Виктор. ЗАО НТЦ Модуль (v.nelyubin@module.ru)
 * @brief эмулятор wdt sp805 для СнК ОИ10
 * @version 0.1
 * @date 2026-09-15
 * 
 * 
 */

#include "qemu/osdep.h"
#include "hw/qdev-properties.h"
#include "hw/ppc/ppc.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/irq.h"
#include "hw/watchdog/wdt_sp805.h"

/**
    \defgroup wdt_sp805_api
    \ingroup wdt_sp805
    \brief функции, макросы и типы для управления моделью WDT ARM SP805
*/

/*
 * ==========================================================================
 * Смещения регистров от базового адреса
 * ==========================================================================
 */

/**
 * @brief Смещение регистра загрузки WdogLoad.
 * 
 * @ingroup wdt_sp805_api
 */
#define SP805_LOAD      0x000

/** @brief Смещение регистра текущего значения WdogValue. */
#define SP805_VALUE     0x004

/** @brief Смещение регистра управления WdogControl. */
#define SP805_CONTROL   0x008

/** @brief Смещение регистра сброса прерывания WdogIntClr. */
#define SP805_INTCLR    0x00C

/** @brief Смещение регистра сырого статуса WdogRIS. */
#define SP805_RIS       0x010

/** @brief Смещение регистра маскированного статуса WdogMIS. */
#define SP805_MIS       0x014

/** @brief Смещение регистра блокировки WdogLock. */
#define SP805_LOCK      0xC00

/** @brief Смещение регистра управления интеграционным тестом WdogITCR. */
#define SP805_ITCR      0xF00

/** @brief Смещение регистра выходных данных интеграционного теста WdogITOP. */
#define SP805_ITOP      0xF04

/** @brief Смещение регистра идентификации периферии WdogPeriphID0. */
#define SP805_PERIPHID0 0xFE0

/** @brief Смещение регистра идентификации периферии WdogPeriphID1. */
#define SP805_PERIPHID1 0xFE4

/** @brief Смещение регистра идентификации периферии WdogPeriphID2. */
#define SP805_PERIPHID2 0xFE8

/** @brief Смещение регистра идентификации периферии WdogPeriphID3. */
#define SP805_PERIPHID3 0xFEC

/** @brief Смещение регистра идентификации PrimeCell WdogPCellID0. */
#define SP805_PCELLID0  0xFF0

/** @brief Смещение регистра идентификации PrimeCell WdogPCellID1. */
#define SP805_PCELLID1  0xFF4

/** @brief Смещение регистра идентификации PrimeCell WdogPCellID2. */
#define SP805_PCELLID2  0xFF8

/** @brief Смещение регистра идентификации PrimeCell WdogPCellID3. */
#define SP805_PCELLID3  0xFFC

/** @brief Последнее допустимое смещение регистра в DCR-пространстве. */
#define SP805_REG_LAST  0xFFC

/*
 * ==========================================================================
 * Битовые определения регистра WdogControl
 * ==========================================================================
 */

/** @brief Бит разрешения счётчика и прерывания (INTEN). */
#define SP805_CONTROL_INTEN  (1u << 0)

/** @brief Бит разрешения выхода сброса (RESEN). */
#define SP805_CONTROL_RESEN  (1u << 1)

/** @brief Маска допустимых битов регистра WdogControl. */
#define SP805_CONTROL_MASK   (SP805_CONTROL_INTEN | SP805_CONTROL_RESEN)

/*
 * ==========================================================================
 * Константы
 * ==========================================================================
 */

/** @brief Магическое значение для разблокировки регистров WdogLock. */
#define SP805_LOCK_MAGIC     0x1ACCE551u

/** @brief Значение регистра WdogLoad после сброса. */
#define SP805_LOAD_RESET_VAL 0xFFFFFFFFu

/*
 * ==========================================================================
 * Фиксированные значения идентификационных регистров
 * ==========================================================================
 */

/** @brief Значение WdogPeriphID0 после сброса. */
#define SP805_PERIPHID0_VAL 0x05u

/** @brief Значение WdogPeriphID1 после сброса. */
#define SP805_PERIPHID1_VAL 0x18u

/** @brief Значение WdogPeriphID2 после сброса. */
#define SP805_PERIPHID2_VAL 0x14u

/** @brief Значение WdogPeriphID3 после сброса. */
#define SP805_PERIPHID3_VAL 0x00u

/** @brief Значение WdogPCellID0 после сброса. */
#define SP805_PCELLID0_VAL  0x0Du

/** @brief Значение WdogPCellID1 после сброса. */
#define SP805_PCELLID1_VAL  0xF0u

/** @brief Значение WdogPCellID2 после сброса. */
#define SP805_PCELLID2_VAL  0x05u

/** @brief Значение WdogPCellID3 после сброса. */
#define SP805_PCELLID3_VAL  0xB1u

/*
 * ==========================================================================
 * Вспомогательные функции
 * ==========================================================================
 */

/**
 * @brief Обновляет регистр MIS и состояние линии прерывания WDOGINT.
 *
 * @param s Указатель на состояние устройства SP805.
 *
 * Вычисляет значение WdogMIS как логическое И между WdogRIS и битом
 * INTEN регистра WdogControl. Управляет линией прерывания в соответствии
 * с полученным значением.
 */
static void sp805_update_irq(SP805State *s)
{
    if (s->ris && (s->control & SP805_CONTROL_INTEN))
    {
        s->mis = 1;
        qemu_irq_raise(s->irq);
    }
    else
    {
        s->mis = 0;
        qemu_irq_lower(s->irq);
    }
}

/**
 * @brief Обрабатывает событие достижения счётчиком нуля.
 *
 * @param s Указатель на состояние устройства SP805.
 * @param stop_timer Флаг необходимости остановки ptimer при генерации сброса.
 *
 * Если флаг WdogRIS уже установлен (прерывание не было сброшено),
 * и бит RESEN разрешает генерацию сброса, ассертится выход WDOGRES.
 * При @p stop_timer == true таймер останавливается.
 * В противном случае устанавливается флаг WdogRIS и обновляется
 * состояние линии прерывания.
 */
static void sp805_trigger(SP805State *s, bool stop_timer)
{
    if (s->ris)
    {
        if (s->control & SP805_CONTROL_RESEN)
        {
            qemu_irq_raise(s->reset);
        }

        if (stop_timer)
        {
            ptimer_stop(s->timer);
        }
    }
    else
    {
        s->ris = 1;
        sp805_update_irq(s);
    }
}

/**
 * @brief Callback-функция ptimer, вызываемая при достижении нуля.
 *
 * @param opaque Указатель на состояние устройства SP805.
 *
 * Функция вызывается внутри транзакции ptimer, поэтому вызов
 * ptimer_stop() допустим без дополнительных begin/commit.
 */
static void sp805_ptimer_cb(void *opaque)
{
    SP805State *s = SP805(opaque);

    sp805_trigger(s, true);
}

/**
 * @brief Запускает счётчик с текущими настройками.
 *
 * @param s Указатель на состояние устройства SP805.
 *
 * Если регистр WdogLoad равен нулю, прерывание генерируется
 * немедленно без использования ptimer. В противном случае
 * ptimer перезагружается из WdogLoad и запускается в периодическом
 * режиме.
 */
static void sp805_start_timer(SP805State *s)
{
    if (s->load == 0)
    {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "SP805 Warning: Запуск WDT с нулевым регистром Load\n");
        sp805_trigger(s, false);
    }
    else
    {
        ptimer_transaction_begin(s->timer);
        ptimer_set_limit(s->timer, s->load, 1);
        ptimer_set_freq(s->timer, s->base_freq);
        ptimer_run(s->timer, 0);
        ptimer_transaction_commit(s->timer);
    }
}

/**
 * @brief Останавливает счётчик.
 *
 * @param s Указатель на состояние устройства SP805.
 */
static void sp805_stop_timer(SP805State *s)
{
    ptimer_transaction_begin(s->timer);
    ptimer_stop(s->timer);
    ptimer_transaction_commit(s->timer);
}

/**
 * @brief Обрабатывает запись в регистр WdogControl.
 *
 * @param s Указатель на состояние устройства SP805.
 * @param val Записываемое значение.
 *
 * Сохраняет только допустимые биты (INTEN и RESEN). При переходе
 * бита INTEN из 0 в 1 запускает счётчик, при переходе из 1 в 0 —
 * останавливает. Обновляет состояние линии прерывания.
 */
static void sp805_write_control(SP805State *s, uint32_t val)
{
    bool was_enabled = s->control & SP805_CONTROL_INTEN;
    bool now_enabled;

    s->control = val & SP805_CONTROL_MASK;
    now_enabled = s->control & SP805_CONTROL_INTEN;

    if (!was_enabled && now_enabled)
    {
        sp805_start_timer(s);
    }
    else if (was_enabled && !now_enabled)
    {
        sp805_stop_timer(s);
    }

    sp805_update_irq(s);
}

/**
 */

/**
 * @brief Обрабатывает запись в регистр WdogIntClr.
 *
 * @param s Указатель на состояние устройства SP805.
 * @ingroup wdt_sp805_api
 *
 * Сбрасывает флаги WdogRIS и WdogMIS, опускает линию прерывания.
 * Если счётчик разрешён, перезагружает его из WdogLoad и запускает.
 */
static void sp805_clear_interrupt(SP805State *s)
{
    s->ris = 0;
    s->mis = 0;
    qemu_irq_lower(s->irq);

    if (s->control & SP805_CONTROL_INTEN)
    {/// перезапуск таймера
        sp805_start_timer(s);
    }
}

/*
 * ==========================================================================
 * Обработчики DCR-регистров
 * ==========================================================================
 */

/**
 * @brief Обработчик чтения DCR-регистра.
 *
 * @param opaque Указатель на состояние устройства SP805.
 * @param dcrn Номер DCR-регистра.
 * @return Значение прочитанного регистра.
 * @ingroup wdt_sp805_api
 *
 * Смещение вычисляется как @c dcrn - baseaddr. Неизвестные и
 * нереализованные регистры вызывают сообщение LOG_UNIMP.
 */
static uint32_t sp805_dcr_read(void *opaque, int dcrn)
{
    SP805State *s = SP805(opaque);
    uint32_t offset = (uint32_t)dcrn - s->baseaddr;

    switch (offset)
    {
    case SP805_LOAD:
        return s->load;

    case SP805_VALUE:
        return (uint32_t)ptimer_get_count(s->timer);

    case SP805_CONTROL:
        return s->control;

    case SP805_INTCLR:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "SP805: чтение из регистра WdogIntClr (только для записи)\n");
        return 0;

    case SP805_RIS:
        return s->ris;

    case SP805_MIS:
        return s->mis;

    case SP805_LOCK:
        return s->lock;

    case SP805_PERIPHID0:
        return SP805_PERIPHID0_VAL;

    case SP805_PERIPHID1:
        return SP805_PERIPHID1_VAL;

    case SP805_PERIPHID2:
        return SP805_PERIPHID2_VAL;

    case SP805_PERIPHID3:
        return SP805_PERIPHID3_VAL;

    case SP805_PCELLID0:
        return SP805_PCELLID0_VAL;

    case SP805_PCELLID1:
        return SP805_PCELLID1_VAL;

    case SP805_PCELLID2:
        return SP805_PCELLID2_VAL;

    case SP805_PCELLID3:
        return SP805_PCELLID3_VAL;

    default:
        qemu_log_mask(LOG_UNIMP,
                      "SP805: чтение из нереализованного регистра 0x%03X\n",
                      offset);
        return 0;
    }
}

/**
 * @brief Обработчик записи DCR-регистра.
 *
 * @param opaque Указатель на состояние устройства SP805.
 * @param dcrn Номер DCR-регистра.
 * @param val Записываемое значение.
 *
 * Смещение вычисляется как @c dcrn - baseaddr. Запись в защищённые
 * регистры при установленной блокировке игнорируется. Регистры
 * интеграционного тестирования вызывают LOG_UNIMP.
 */
static void sp805_dcr_write(void *opaque, int dcrn, uint32_t val)
{
    SP805State *s = SP805(opaque);
    uint32_t offset = (uint32_t)dcrn - s->baseaddr;

    /* Регистр блокировки доступен для записи всегда */
    if (offset == SP805_LOCK)
    {
        s->lock = (val == SP805_LOCK_MAGIC) ? 0 : 1;
        return;
    }

    /* Остальные регистры защищены блокировкой */
    if (s->lock)
    {
        return;
    }

    switch (offset)
    {
    case SP805_LOAD:
        s->load = val;

        if (s->control & SP805_CONTROL_INTEN)
        {
            sp805_start_timer(s);
        }

        break;

    case SP805_CONTROL:
        sp805_write_control(s, val);
        break;

    case SP805_INTCLR:
        sp805_clear_interrupt(s);
        break;

    case SP805_ITCR:
    case SP805_ITOP:
        qemu_log_mask(LOG_UNIMP,
                      "SP805: запись в регистр интеграционного тестирования 0x%03X\n",
                      offset);
        break;

    case SP805_VALUE:
    case SP805_RIS:
    case SP805_MIS:
    case SP805_PERIPHID0:
    case SP805_PERIPHID1:
    case SP805_PERIPHID2:
    case SP805_PERIPHID3:
    case SP805_PCELLID0:
    case SP805_PCELLID1:
    case SP805_PCELLID2:
    case SP805_PCELLID3:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "SP805: запись в регистр только для чтения 0x%03X\n",
                      offset);
        break;

    default:
        qemu_log_mask(LOG_UNIMP,
                      "SP805: запись в нереализованный регистр 0x%03X\n",
                      offset);
        break;
    }
}

/*
 * ==========================================================================
 * Жизненный цикл устройства
 * ==========================================================================
 */

/**
 * @brief Сброс состояния устройства.
 *
 * @param dev Указатель на устройство.
 *
 * Останавливает таймер, сбрасывает все регистры в значения по умолчанию,
 * опускает линии прерывания и сброса.
 */
static void sp805_reset(DeviceState *dev)
{
    SP805State *s = SP805(dev);
    s->load = SP805_LOAD_RESET_VAL;
    if (s->timer)
    {
        ptimer_transaction_begin(s->timer);
        ptimer_stop(s->timer);
        ptimer_set_limit(s->timer, s->load, 1);
        ptimer_transaction_commit(s->timer);
    }

    s->control = 0;
    s->lock = 0;// после перезагрузки контроллер открыт
    s->ris = 0;
    s->mis = 0;

    qemu_irq_lower(s->irq);
    qemu_irq_lower(s->reset);
}

/**
 * @brief Инициализация экземпляра устройства.
 *
 * @param obj Указатель на объект.
 *
 * Создаёт именованные выходы GPIO для линий прерывания и сброса.
 */
static void sp805_init(Object *obj)
{
    SP805State *s = SP805(obj);

    qdev_init_gpio_out_named(DEVICE(obj), &s->irq, "wdt_irq", 1);
    qdev_init_gpio_out_named(DEVICE(obj), &s->reset, "wdt_res", 1);
}

/**
 * @brief Реализация устройства.
 *
 * @param dev Указатель на устройство.
 * @param errp Указатель на объект ошибки.
 *
 * Проверяет обязательные свойства, создаёт ptimer и регистрирует
 * DCR-обработчики для всего диапазона регистров устройства.
 */
static void sp805_realize(DeviceState *dev, Error **errp)
{
    SP805State *s = SP805(dev);
    PowerPCCPU *cpu = NULL;
    CPUPPCState *env = NULL;
    uint32_t offset = 0;

    if (!s->cpu)
    {
        error_setg(errp, "SP805: свойство 'cpu-state' не задано");
        return;
    }

    if (s->base_freq == 0)
    {
        error_setg(errp, "SP805: базовая частота не может быть нулевой");
        return;
    }

    /* Создание внутреннего таймера */
    s->timer = ptimer_init(sp805_ptimer_cb, s, PTIMER_POLICY_LEGACY);
    if (!s->timer)
    {
        error_setg(errp, "SP805: не удалось создать ptimer");
        return;
    }

    ptimer_transaction_begin(s->timer);
    ptimer_set_freq(s->timer, s->base_freq);
    ptimer_transaction_commit(s->timer);

    /* Регистрация DCR-обработчиков для всего диапазона регистров */
    cpu = POWERPC_CPU(s->cpu);
    env = &cpu->env;

    for (offset = 0; offset <= SP805_REG_LAST; offset += 4)
    {
        ppc_dcr_register(env, s->baseaddr + offset, s,
                         sp805_dcr_read, sp805_dcr_write);
    }
    sp805_reset(dev);
}

/**
 * @brief Деинициализация устройства.
 *
 * @param dev Указатель на устройство.
 *
 * Освобождает ресурсы ptimer.
 */
static void sp805_unrealize(DeviceState *dev)
{
    SP805State *s = SP805(dev);

    if (s->timer)
    {
        ptimer_free(s->timer);
        s->timer = NULL;
    }
}

/*
 * ==========================================================================
 * Свойства и класс устройства
 * ==========================================================================
 */

/**
 * @brief Массив свойств устройства SP805.
 *
 * - "cpu-state" — ссылка на процессор (TYPE_CPU).
 * - "base-freq" — базовая частота счётчика, по умолчанию 25 МГц.
 * - "baseaddr" — базовый адрес в DCR-пространстве, по умолчанию 0x800B0000.
 */
static Property sp805_properties[] =
{
    DEFINE_PROP_LINK("cpu-state", SP805State, cpu, TYPE_CPU, CPUState *),
    DEFINE_PROP_UINT32("base-freq", SP805State, base_freq, 25000000),
    DEFINE_PROP_UINT32("baseaddr", SP805State, baseaddr, 0x800B0000),
    DEFINE_PROP_END_OF_LIST(),
};

/**
 * @brief Инициализация класса устройства.
 *
 * @param klass Указатель на класс объекта.
 * @param data Дополнительные данные (не используются).
 * @ingroup
 */
static void sp805_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = sp805_realize;
    dc->unrealize = sp805_unrealize;
    dc->reset = sp805_reset;
    dc->desc = "ARM SP805 Watchdog";
    device_class_set_props(dc, sp805_properties);
}

/**
 * @brief Информация о типе устройства SP805.
 */
static const TypeInfo sp805_info =
{
    .name          = TYPE_SP805,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(SP805State),
    .instance_init = sp805_init,
    .class_init    = sp805_class_init,
};

/**
 * @brief Регистрация типа устройства.
 */

static void sp805_register_types(void)
{
    type_register_static(&sp805_info);
}

type_init(sp805_register_types);
