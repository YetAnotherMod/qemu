
#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"
#include "qemu/log.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "hw/ptimer.h"
#include "migration/vmstate.h"
#include "hw/irq.h"
#include "hw/timer/double_timer.h"

/*
 * Forward declarations.
 */
static void double_timer_update_irq(TimerUnitState *tu);
static void double_timer_recalc_freq(uint64_t base_freq_hz, TimerUnitState *tu);
static void double_timer_restart_safe(TimerUnitState *tu);
static void double_timer_stop_safe(TimerUnitState *tu);

static uint64_t double_timer_get_current_count(TimerUnitState *tu);

/*
 * ptimer callback 
 * Called when the ptimer reaches zero.
 */
static void double_timer_ptimer_cb(void *opaque)
{
    TimerUnitState *tu = opaque;
    /*
     * Set raw interrupt status (RIS).
     * This will also update MIS and the IRQ line if interrupts are enabled.
     */
    tu->ris = 1;
    double_timer_update_irq(tu);

    /*
     * Apply background load if pending.
     * The bg_load value is copied to load and will be used for the next period.
     */
    if (tu->bg_load_pending)
    {
        tu->load = tu->bg_load;
        tu->bg_load_pending = false;
        ptimer_transaction_begin(tu->ptimer);
        ptimer_set_limit(tu->ptimer, tu->bg_load, 1);// reset to new limit
        ptimer_transaction_commit(tu->ptimer);
    }
}

/*
 * Update the masked interrupt status (MIS) and the IRQ line.
 */
static void double_timer_update_irq(TimerUnitState *tu)
{
    /*
     * MIS is 1 if RIS is 1 and interrupts are enabled in the control register.
     */
    if (tu->ris && (tu->control & CONTROL_INT_ENABLE))
    {
        tu->mis = 1;
        qemu_irq_raise(tu->irq);
    }
    else
    {
        tu->mis = 0;
        qemu_irq_lower(tu->irq);
    }
}

/*
 * Recalculate the timer frequency and period based on the base frequency
 * and the prescaler value in the control register.
 */
static void double_timer_recalc_freq(uint64_t base_freq_hz, TimerUnitState *tu)
{
    uint32_t prescale_val = PRESCALE_1;
    uint32_t divider = 0;

    prescale_val =
        (tu->control >> CONTROL_PRESCALE_SHIFT) & CONTROL_PRESCALE_MASK;

    switch (prescale_val)
    {
    case 0: /* 00 */
        divider = PRESCALE_1;
        break;
    case 1: /* 01 */
        divider = PRESCALE_16;
        break;
    case 2: /* 10 */
        divider = PRESCALE_256;
        break;
    default: /* 11 - reserved, treat as 1 */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "WARNING! DIT module. Invalid divider(%u). Set to default(%u)",
                      prescale_val, PRESCALE_1);
        divider = PRESCALE_1;
    }

    tu->freq_hz = base_freq_hz / divider;
    if (tu->freq_hz == 0)
    {// на всяк случ
        tu->freq_hz = 1; 
        qemu_log_mask(LOG_GUEST_ERROR, "WARNING! DIT module. Timer frequency is 1HZ");
    }
}

/**
 * @brief рассчитывает reload значение для таймера
 * 
 * @param tu - указатель на структуру таймера
 * @return uint64_t  рассчитанное значение reload
 */
static uint64_t double_timer_calc_limit(TimerUnitState *tu)
{
    uint64_t limit = 0;
    /*
     * Determine the limit:
     * - If TIMER_MODE_LOAD is 0 (count from max), use max_val.
     * - If TIMER_MODE_LOAD is 1 (count from load), use tu->load.
     */
    if (tu->control & CONTROL_TIMER_MODE_LOAD)
    {
        limit = (uint64_t)tu->load;
        if ((tu->control & CONTROL_TIMER_SIZE_32) == 0)
        { // на всякий случай проверить разрядность числа перезагрузки
            if (limit > COUNTER_MAX_16)
            {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "WARNING! Значение регистра загрузки(%u) "
                              "превышает максимальное для режима 16 бит",
                              tu->load);
            }
        }
    }
    else
    { // свободный счет
        limit = ((tu->control & CONTROL_TIMER_SIZE_32) == 0) ? COUNTER_MAX_16 :
                                                               COUNTER_MAX_32;
    }
    return limit;
}

/*
 * UNSAFE!
 * (Re)start the timer with the current settings.
 * Warning!  This function assumes the caller has already called
 * ptimer_transaction_begin() on the ptimer.
 */
static void double_timer_restart(TimerUnitState *tu)
{
    uint64_t limit;

    if((tu->load == 0) && (tu->control & CONTROL_TIMER_MODE_LOAD))
    {
        qemu_log_mask(LOG_GUEST_ERROR, "ERROR! Попытка запуска DIT с нулевым регистром"
                                            " загрузки. Игнорирую команду");
        return;
    }

    limit = double_timer_calc_limit(tu);

    /* Set the ptimer limit and reload */
    ptimer_set_limit(tu->ptimer, limit, 1);

    /* Set the frequency */
    ptimer_set_freq(tu->ptimer, tu->freq_hz);

    /* Run the ptimer: 0 = periodic, 1 = one-shot */
    if (tu->control & CONTROL_ONE_SHOT)
    {
        ptimer_run(tu->ptimer, 1);
    }
    else
    {
        ptimer_run(tu->ptimer, 0);
    }
}

/*
 * Public wrapper for restarting the timer by double_timer_restart function.
 * Handles the ptimer transaction.
 */
static void double_timer_restart_safe(TimerUnitState *tu)
{
    ptimer_transaction_begin(tu->ptimer);
    double_timer_restart(tu);
    ptimer_transaction_commit(tu->ptimer);
}

/*
 * Stop the timer.
 */
static void double_timer_stop_safe(TimerUnitState *tu)
{
    ptimer_transaction_begin(tu->ptimer);
    ptimer_stop(tu->ptimer);
    ptimer_transaction_commit(tu->ptimer);
}

/*
 * Get the current counter value from the ptimer.
 */
static uint64_t double_timer_get_current_count(TimerUnitState *tu)
{
    return ptimer_get_count(tu->ptimer);
}

/*
 * MMIO read handler.
 */
static uint64_t double_timer_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    DoubleTimerState *s = (DoubleTimerState *)opaque;
    TimerUnitState *tu = NULL;
    int timer_idx = 0;
    uint64_t ret = 0;

    if (size != 4)
    {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid read size %u at 0x%" HWADDR_PRIx "\n",
                      __func__, size, addr);
        return 0;
    }

    /*
     * вычислить индекс таймера
     */
    if (addr < TIMER_REG_BLOCK_SZB)
    {
        timer_idx = 0;
    }
    else if (addr < (TIMER_REG_BLOCK_SZB * 2))
    {
        timer_idx = 1;
        /* Offset within the timer's register block */
        addr -= TIMER_REG_BLOCK_SZB;
    }
    else
    {
        /* Peripheral ID or PCell ID registers. Возвращаем фиксированное значение*/
        switch (addr)
        {
        case PERIPH_ID_0_OFFSET:
            return PERIPH_ID_0_VAL;
        case PERIPH_ID_1_OFFSET:
            return PERIPH_ID_1_VAL;
        case PERIPH_ID_2_OFFSET:
            return PERIPH_ID_2_VAL;
        case PERIPH_ID_3_OFFSET:
            return PERIPH_ID_3_VAL;
        case PCELL_ID_0_OFFSET:
            return PCELL_ID_0_VAL;
        case PCELL_ID_1_OFFSET:
            return PCELL_ID_1_VAL;
        case PCELL_ID_2_OFFSET:
            return PCELL_ID_2_VAL;
        case PCELL_ID_3_OFFSET:
            return PCELL_ID_3_VAL;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: invalid read at 0x%" HWADDR_PRIx "\n", __func__,
                          addr);
            return 0;
        }
    }
    /// сюда попадаем только для адресов внутри блока регистров таймеров
    tu = &s->timers[timer_idx];

    switch (addr)
    {
    case TIMER_LOAD_OFFSET(0):
        ret = (uint64_t)tu->load;
        break;
    case TIMER_VALUE_OFFSET(0):
        ret = double_timer_get_current_count(tu);
        break;
    case TIMER_CONTROL_OFFSET(0):
        ret = (uint64_t)tu->control;
        break;
    case TIMER_RIS_OFFSET(0):
        ret = (uint64_t)tu->ris;
        break;
    case TIMER_MIS_OFFSET(0):
        ret = (uint64_t)tu->mis;
        break;
    case TIMER_BG_LOAD_OFFSET(0):
        ret = (uint64_t)tu->bg_load;
        break;
    case TIMER_INT_CLR_OFFSET(0):
        /* Reads from IntClr are undefined; return 0 */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Warning. TimerXIntClr is write only", __func__);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid read at 0x%" HWADDR_PRIx " (timer %d)\n",
                      __func__, addr, timer_idx);
    }

    return ret;
}

/*
 * MMIO write handler.
 */
static void double_timer_mmio_write(void *opaque, hwaddr addr,
                                uint64_t value, unsigned size)
{
    DoubleTimerState *s = opaque;
    TimerUnitState *tu = NULL;
    int timer_idx = 0;
    uint32_t old_control = 0;
    bool timer_was_enabled = false;
    uint64_t limit = 0;
    uint32_t tmp = 0;

    /* All registers are 32-bit */
    if (size != 4)
    {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid write size %u at 0x%" HWADDR_PRIx "\n",
                      __func__, size, addr);
        return;
    }

    /*
     * вычисляем таймер
     */
    if (addr < TIMER_REG_BLOCK_SZB)
    {
        timer_idx = 0;
    }
    else if (addr < (TIMER_REG_BLOCK_SZB * 2))
    {
        timer_idx = 1;
        addr -= TIMER_REG_BLOCK_SZB;
    }
    else
    {
        /* Peripheral ID and PCell ID registers are read-only */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to read-only register at 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        return;
    }

    tu = &s->timers[timer_idx];
    switch (addr)
    {
    case TIMER_LOAD_OFFSET(0):
        /* TimerXLoad: write to load register */
        tu->load = value;
        if((tu->control & CONTROL_TIMER_MODE_LOAD) == CONTROL_TIMER_MODE_LOAD)
        {
            limit = double_timer_calc_limit(tu);
            ptimer_transaction_begin(tu->ptimer);
            ptimer_set_limit(tu->ptimer, limit, 1);
            ptimer_transaction_commit(tu->ptimer);
        }
        else// таймер в свободном счете и регистр загрузки не влияет
        {
            qemu_log_mask(LOG_GUEST_ERROR, "Warning! Таймер в свободном "
                                           "режме и запись в TimerXLoad не "
                                           "влияет на его работу");
        }
        break;
    case TIMER_CONTROL_OFFSET(0):
        tmp = (uint32_t)value;
        /* TimerXControl: write to control register */
        old_control = tu->control;
        timer_was_enabled = old_control & CONTROL_TIMER_EN;
        ////////////////
        if(timer_was_enabled)
        {// если таймер включен, то сначала нужно выключить его
            if((tmp & CONTROL_TIMER_EN) == 0)
            {// выключить таймер
                double_timer_stop_safe(tu);
                /// @todo разобраться, можно ли выключить таймер и изменить настройки одной командой
            }
            else
            {// таймер включен и менять настройки на ходу нельзя
                qemu_log_mask(LOG_GUEST_ERROR,
                              "%s: prescaler change ignored (timer %d is enabled)\n",
                              __func__, timer_idx);
                return;
            }
        }
        /// если мы здесь, то таймер уже выключен
        tu->control = tmp;
        double_timer_recalc_freq(s->base_freq_hz, tu);
        if ((tu->control & CONTROL_TIMER_EN) == CONTROL_TIMER_EN)
        { /* запускаем таймер */
            double_timer_restart_safe(tu);
        }
        break;

    case TIMER_INT_CLR_OFFSET(0):
        /* TimerXIntClr: write any value to clear the interrupt */
        tu->ris = 0;
        tu->mis = 0;
        double_timer_update_irq(tu);
        break;

    case TIMER_BG_LOAD_OFFSET(0):
        /* TimerXBGLoad: write to background load register */
        tu->bg_load = value;

        /*
        если таймер в данный момент запущен, то подменим регистр при достижении нуля
        */
        if ((tu->control & CONTROL_TIMER_EN) && (ptimer_get_count(tu->ptimer) > 0))
        {
            tu->bg_load_pending = true;
        }
        else
        {
            tu->load = tu->bg_load;
            tu->bg_load_pending = false;
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid write at 0x%" HWADDR_PRIx " (timer %d)\n",
                      __func__, addr, timer_idx);
    }
}

/*
 * MMIO operation table.
 */
static const MemoryRegionOps double_timer_mmio_ops = {
    .read = double_timer_mmio_read,
    .write = double_timer_mmio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

/*
 * Device reset.
 */
static void double_timer_reset(DeviceState *dev)
{
    DoubleTimerState *s = DOUBLE_TIMER(dev);

    for (int i = 0; i < NUM_TIMERS; i++)
    {
        TimerUnitState *tu = &s->timers[i];

        /* Stop the ptimer */
        double_timer_stop_safe(tu);

        /* Reset registers to default values */
        // странно - запрещенное значение после перезагрузки(см РЭ)
        tu->load = 0;
        // странно - запрещенное значение после перезагрузки(см РЭ)
        tu->bg_load = 0;
        tu->bg_load_pending = false;
        /*
         * - TimerEn (bit 7): default 0
         * - TimerMode (bit 6): default 0
         * - IntEnable (bit 5): default 1 
         * - TimerPre (bits 3-2): default 0 (divider 1)
         * - TimerSize (bit 1): default 0 (16-bit)
         * - OneShot (bit 0): default 0 (periodic)
         */
        //! странно - прерывание включено по умолчанию
        tu->control = CONTROL_VAL_DEFAULT; 
        tu->ris = 0;
        tu->mis = 0;
        /* Update IRQ line */
        double_timer_update_irq(tu);

        /* Recalculate frequency */
        double_timer_recalc_freq(s->base_freq_hz ,tu);
    }
}

/*
 * Device realize.
 */
static void double_timer_realize(DeviceState *dev, Error **errp)
{
    DoubleTimerState *s = DOUBLE_TIMER(dev);
    /// @todo задать какой то минимальный порог частоты
    if (s->base_freq_hz == 0)
    {
        error_setg(errp, "Error. Invalid base frequency in DIT module(%u)",
                   s->base_freq_hz);
    }
    else
    {
        for (int i = 0; i < NUM_TIMERS; i++)
        {
            TimerUnitState *tu = &s->timers[i];

            tu->ptimer = ptimer_init(double_timer_ptimer_cb, tu,
                                     PTIMER_POLICY_CONTINUOUS_TRIGGER);
            ptimer_transaction_begin(tu->ptimer);
            /// по умолчанию делитель равен единице
            ptimer_set_freq(tu->ptimer, s->base_freq_hz);
            ///! значение timerXload по умолчанию - ноль. Но это запрещено в РЭ
            ptimer_set_limit(tu->ptimer, tu->load, 1);
            /// начальное значение timerXValue - 0xFFFFFFFF
            /// это странно - так как режим по умолчанию - счетчик 16 бит 
            ptimer_set_count(tu->ptimer, UINT32_MAX);
            ptimer_transaction_commit(tu->ptimer);
        }
    }
}

/*
 * Device unrealize (cleanup).
 */
static void double_timer_unrealize(DeviceState *dev)
{
    DoubleTimerState *s = DOUBLE_TIMER(dev);

    for (int i = 0; i < NUM_TIMERS; i++)
    {
        TimerUnitState *tu = &s->timers[i];
        double_timer_stop_safe(tu);
        /* Delete the ptimer */
        ptimer_free(tu->ptimer);
        tu->ptimer = NULL;
    }
}

/*
 * Property definitions.
 */
static Property double_timer_properties[] = {
    DEFINE_PROP_UINT32("dit-freq-hz", DoubleTimerState, base_freq_hz, 25000000),
    DEFINE_PROP_END_OF_LIST(),
};

/*
 * Class initialization.
 */
static void double_timer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = double_timer_realize;
    dc->unrealize = double_timer_unrealize;
    dc->reset = double_timer_reset;
    dc->desc = "dual-interval-timer module. You can set base frequency by "
               "\"dit-freq-hz\" property. Default frequency 25MHz";
    device_class_set_props(dc, double_timer_properties);
}

/*
 * Instance initialization.
 */
static void double_timer_init(Object *obj)
{
    DoubleTimerState *s = DOUBLE_TIMER(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    /* Initialize the MMIO region */
    memory_region_init_io(&s->iomem, obj, &double_timer_mmio_ops, s,
                          "double-timer-mmio", DOUBLE_TIMER_MMIO_SZB);
    sysbus_init_mmio(sbd, &s->iomem);

    /* Initialize interrupt lines for each timer */
    for (int i = 0; i < NUM_TIMERS; i++)
    {
        sysbus_init_irq(sbd, &s->timers[i].irq);
        s->timers[i].load = 0;
        s->timers[i].bg_load = 0;
        s->timers[i].control = CONTROL_VAL_DEFAULT;
        s->timers[i].ris = 0;
        s->timers[i].mis = 0;
        s->timers[i].bg_load_pending = false;
    }
}

/*
 * Type information.
 */
static const TypeInfo double_timer_info = 
{
    .name = TYPE_DOUBLE_TIMER,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(DoubleTimerState),
    .instance_init = double_timer_init,
    .class_init = double_timer_class_init,
};

/*
 * Registration function.
 */
static void double_timer_register(void)
{
    type_register_static(&double_timer_info);
}
type_init(double_timer_register);