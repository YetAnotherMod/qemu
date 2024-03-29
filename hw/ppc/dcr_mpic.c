#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "hw/qdev-properties.h"
#include "cpu.h"
#include "hw/ppc/ppc.h"
#include "hw/ppc/dcr_mpic.h"
#include "hw/irq.h"

#define REGS_MASK           0xfffff

/* 128 registers (VP and DST) starting from 0x10000 to 0x20000
 * DST registers will have 0x10 bit in their address
 * starting from 5th bit we have it number from 0 to 127
 */
#define REG_EXT_START       0x10000
#define REG_EXT_END         0x20000
#define REG_DST_MASK        0x10
#define REG_EXT_ID_SHIFT    5
#define REG_EXT_ID_MASK     0x7f

#define REG_CPU_MASK        0xff000
#define REG_CPU0_OFFSET     0x20000
#define REG_CPU1_OFFSET     0x21000
#define REG_CPU2_OFFSET     0x22000
#define REG_CPU3_OFFSET     0x23000

#define REG_IPID_0          0x40
#define REG_IPID_1          0x50
#define REG_IPID_2          0x60
#define REG_IPID_3          0x70

#define REG_TASK_PRIO       0x80
#define REG_WHO_AM_I        0x90

#define REG_NON_CRIT_IAR    0xa0
#define REG_NON_CRIT_EOI    0xb0
#define REG_CRIT_IAR        0xc0
#define REG_CRIT_EOI        0xd0
#define REG_MCHECK_IAR      0xe0
#define REG_MCHECK_EOI      0xf0

#define REG_FEATURE_REP     0x1000
#define REG_GLOBAL_CFG      0x1020
#define REG_VENDOR_INT_TYPE 0x1040

#define REG_RAW_INT_DEBUG   0x1050
#define REG_SOFT_CORE_REV   0x1070
#define REG_VENDOR_ID       0x1080
#define REG_PINI            0x1090

#define REG_IPI_VP_0        0x10a0
#define REG_IPI_VP_1        0x10b0
#define REG_IPI_VP_2        0x10c0
#define REG_IPI_VP_3        0x10d0

#define REG_SPV             0x10e0

#define REG_TIMER_FREQ      0x10f0

#define REG_TIMER_COUNT_0   0x1100
#define REG_TIMER_COUNT_1   0x1140
#define REG_TIMER_COUNT_2   0x1180
#define REG_TIMER_COUNT_3   0x11c0

#define REG_TIMER_BASE_0    0x1110
#define REG_TIMER_BASE_1    0x1150
#define REG_TIMER_BASE_2    0x1190
#define REG_TIMER_BASE_3    0x11d0

#define REG_TIMER_VP_0      0x1120
#define REG_TIMER_VP_1      0x1160
#define REG_TIMER_VP_2      0x11a0
#define REG_TIMER_VP_3      0x11e0

#define REG_TIMER_DEST_0    0x1130
#define REG_TIMER_DEST_1    0x1170
#define REG_TIMER_DEST_2    0x11b0
#define REG_TIMER_DEST_3    0x11f0

#define VP_VECTOR_SHIFT     (31 - 31)
#define VP_PRIORITY_SHIFT   (31 - 15)
#define VP_SENSE_SHIFT      (31 - 9)
#define VP_POLARITY_SHIFT   (31 - 8)
#define VP_ACTIVITY_SHIFT   (31 - 1)
#define VP_MASK_SHIFT       (31 - 0)

#define TASK_PRIO_MASK      0xf

#define SPV_VECTOR_MASK     0xff

#define GLOBAL_CFG_8259     0x20000000
#define GLOBAL_CFG_RESET    0x80000000

#define VITC_BORDER_DEFAULT 0x10
#define VITC_BORDER_MASK    0x1f
#define VITC_MCHECK_SHIFT   (31 - 23)

#define TIMER_TOGGLE_SHIFT  (31 - 0)
#define TIMER_CI_SHIFT      (31 - 0)
#define TIMER_COUNT_MASK    0x7fffffff

// Implementation dependent parameters (do we have 1 or 3 in this reg?)
#define MPIC_FRG            (127 << (31 - 15) | 3 << (31 - 23) | 2)
#define DCR_BAS             0xf

#define TIMER_0_INDEX       (EXT_SOURCE_NUM + 0)
#define TIMER_1_INDEX       (EXT_SOURCE_NUM + 1)
#define TIMER_2_INDEX       (EXT_SOURCE_NUM + 2)
#define TIMER_3_INDEX       (EXT_SOURCE_NUM + 3)

#define IPI_0_INDEX         (EXT_SOURCE_NUM + MAX_TIMER_NUM + 0)
#define IPI_1_INDEX         (EXT_SOURCE_NUM + MAX_TIMER_NUM + 1)
#define IPI_2_INDEX         (EXT_SOURCE_NUM + MAX_TIMER_NUM + 2)
#define IPI_3_INDEX         (EXT_SOURCE_NUM + MAX_TIMER_NUM + 3)

// FIXME: whenever this constant is used -> make code work for all CPU
static const uint32_t cpu_index = 0;

static void *mpic_get_current_irq(MpicState *s, output_type_t type)
{
    GList *last = g_list_last(s->current_irq_list[cpu_index][type]);

    if (last) {
        return last->data;
    }

    return NULL;
}

static void *mpic_remove_current_irq(MpicState *s, output_type_t type)
{
    irq_config_t *current_irq = mpic_get_current_irq(s, type);
    assert(current_irq != NULL);

    s->current_irq_list[cpu_index][type] =
        g_list_remove(s->current_irq_list[cpu_index][type], current_irq);

    return current_irq;
}

static void mpic_set_current_irq(MpicState *s, output_type_t type, irq_config_t *irq)
{
    s->current_irq_list[cpu_index][type] =
        g_list_append(s->current_irq_list[cpu_index][type], irq);
}

static void mpic_update_pending_irq(MpicState *s, output_type_t type, irq_config_t *irq)
{
    /* check if there is no active irqs of any type with higher priority then current */
    for (output_type_t i = type; i < OUTPUT_IRQ_NUM; i++) {
        irq_config_t *current_irq = mpic_get_current_irq(s, i);

        if (current_irq != NULL && irq->priority <= current_irq->priority) {
            return;
        }
    }

    if (s->pending_irqs[cpu_index][type] == NULL ||
        irq->priority > s->pending_irqs[cpu_index][type]->priority) {
        s->pending_irqs[cpu_index][type] = irq;
    }
}

static int get_output_type(MpicState *s, int prio)
{
    if (prio >= s->vitc_mcheck_border) {
        return OUTPUT_MCHECK;
    } else if (prio >= s->vitc_crit_border) {
        return OUTPUT_CRIT;
    }

    return OUTPUT_NON_CRIT;
}

static void mpic_update_irq(MpicState *s)
{
    if (s->task_prio[cpu_index] == TASK_PRIO_MASK) {
        // disable all irqs to this processor
        qemu_irq_lower(s->output_irq[OUTPUT_NON_CRIT]);
        qemu_irq_lower(s->output_irq[OUTPUT_CRIT]);
        qemu_irq_lower(s->output_irq[OUTPUT_MCHECK]);
        return;
    }

    // check all pending irqs to find the highest prio
    irq_config_t *pending_local[1][OUTPUT_IRQ_NUM] = { { NULL, NULL, NULL} };

    for (int i = 0; i < ARRAY_SIZE(s->irq); i++) {
        if (!s->irq[i].pending || s->irq[i].masked) {
            continue;
        }

        if (s->irq[i].priority <= s->task_prio[cpu_index]) {
            continue;
        }

        int output_type = get_output_type(s, s->irq[i].priority);
        if (pending_local[cpu_index][output_type] == NULL ||
            s->irq[i].priority > pending_local[cpu_index][output_type]->priority) {
            pending_local[cpu_index][output_type] = &s->irq[i];
        }
    }

    qemu_mutex_lock(&s->mutex);

    for (output_type_t i = OUTPUT_NON_CRIT; i < OUTPUT_IRQ_NUM; i++) {
        if (pending_local[cpu_index][i]) {
            mpic_update_pending_irq(s, i, pending_local[cpu_index][i]);
        }

        if (s->pending_irqs[cpu_index][i] && s->pending_irqs[cpu_index][i]->pending) {
            qemu_irq_raise(s->output_irq[i]);
        } else {
            qemu_irq_lower(s->output_irq[i]);
        }
    }

    qemu_mutex_unlock(&s->mutex);
}

static void mpic_reset(MpicState *s)
{
    for (int i = 0; i < ARRAY_SIZE(s->irq); i++) {
        memset(&s->irq[i], 0, sizeof(s->irq[i]));
        s->irq[i].masked = true;
        s->irq[i].polarity = true;
    }

    for (int i = 0; i < MAX_CPU_SUPPORTED; i++) {
        s->task_prio[i] = TASK_PRIO_MASK;
    }

    s->spv = SPV_VECTOR_MASK;

    s->pass_through_8259 = true;

    s->vitc_crit_border = VITC_BORDER_DEFAULT;
    s->vitc_mcheck_border = VITC_BORDER_DEFAULT;

    memset(s->pending_irqs, 0, sizeof(s->pending_irqs));

    for (int i = 0; i < OUTPUT_IRQ_NUM; i++) {
        g_list_free(s->current_irq_list[cpu_index][i]);
        s->current_irq_list[cpu_index][i] = NULL;
    }

    for (int i = 0; i < MAX_TIMER_NUM; i++) {
        s->timer_data[i].count = 0;
        s->timer_data[i].active = 0;
        s->timer_data[i].toggle_bit = 0;

        timer_del(&s->qemu_timer[i]);
    }

    mpic_update_irq(s);
}

static inline uint32_t mpic_get_internal_vp_reg(MpicState *s, uint32_t index)
{
    return (
        s->irq[index].vector   << VP_VECTOR_SHIFT      |
        s->irq[index].priority << VP_PRIORITY_SHIFT    |
        s->irq[index].activity << VP_ACTIVITY_SHIFT    |
        s->irq[index].masked   << VP_MASK_SHIFT
    );
}

static inline void mpic_set_internal_vp_reg(MpicState *s, uint32_t index, uint32_t val)
{
    s->irq[index].vector   = val >> VP_VECTOR_SHIFT;
    s->irq[index].priority = val >> VP_PRIORITY_SHIFT;
    s->irq[index].masked   = val >> VP_MASK_SHIFT;
}

static uint32_t mpic_return_current_irq(MpicState *s, output_type_t type)
{
    qemu_mutex_lock(&s->mutex);
    if (s->pending_irqs[cpu_index][type] == NULL) {
        qemu_mutex_unlock(&s->mutex);
        return s->spv;
    }

    qemu_irq_lower(s->output_irq[type]);

    irq_config_t *current_irq = s->pending_irqs[cpu_index][type];
    s->pending_irqs[cpu_index][type] = NULL;

    mpic_set_current_irq(s, type, current_irq);

    current_irq->activity = true;

    // clear pending bit (edge-triggered, inter-process or timer)
    if (!current_irq->sense) {
        current_irq->pending = false;
    }

    qemu_mutex_unlock(&s->mutex);
    return current_irq->vector;
}

static uint32_t mpic_get_timer_count(MpicState *s, uint32_t index)
{
    uint32_t count = 0;

    qemu_mutex_lock(&s->mutex);

    if (timer_pending(s->qemu_timer)) {
        count =
            timer_expire_time_ns(s->qemu_timer) - qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    }

    qemu_mutex_unlock(&s->mutex);

    return s->timer_data[index - TIMER_0_INDEX].toggle_bit << TIMER_TOGGLE_SHIFT | count;
}

static inline uint32_t mpic_get_timer_base(MpicState *s, uint32_t index)
{
    uint32_t val;

    val = s->timer_data[index - TIMER_0_INDEX].active ? 0 : 1;
    val <<= TIMER_CI_SHIFT;

    val |= s->timer_data[index - TIMER_0_INDEX].count;

    return val;
}

static void mpic_set_timer_base(MpicState *s, uint32_t index, uint32_t new_val)
{
    s->timer_data[index - TIMER_0_INDEX].count = new_val & TIMER_COUNT_MASK;

    if (new_val & (1 << TIMER_CI_SHIFT)) {
        /* stop active timer */
        if (s->timer_data[index - TIMER_0_INDEX].active) {
            timer_del(&s->qemu_timer[index - TIMER_0_INDEX]);
        }

        s->timer_data[index - TIMER_0_INDEX].active = false;
    } else {
        /* start inactive timer */
        if (!s->timer_data[index - TIMER_0_INDEX].active) {
            timer_mod(&s->qemu_timer[index - TIMER_0_INDEX],
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                      muldiv64(s->timer_data[index - TIMER_0_INDEX].count,
                               NANOSECONDS_PER_SECOND, s->timer_freq));
        }

        s->timer_data[index - TIMER_0_INDEX].active = true;
    }
}

static uint32_t mpic_dcr_read (void *opaque, int dcrn)
{
    MpicState *s = MPIC(opaque);

    dcrn &= REGS_MASK;

    if (dcrn >= REG_EXT_START && dcrn < REG_EXT_END) {
        int id = (dcrn >> REG_EXT_ID_SHIFT) & REG_EXT_ID_MASK;

        if (dcrn & REG_DST_MASK) {
            return s->irq[id].destination;
        } else {
            return (
                s->irq[id].vector   << VP_VECTOR_SHIFT      |
                s->irq[id].priority << VP_PRIORITY_SHIFT    |
                s->irq[id].sense    << VP_SENSE_SHIFT       |
                s->irq[id].polarity << VP_POLARITY_SHIFT    |
                s->irq[id].activity << VP_ACTIVITY_SHIFT    |
                s->irq[id].masked   << VP_MASK_SHIFT
            );
        }
    }

    switch (dcrn) {
    case REG_FEATURE_REP:
        return MPIC_FRG;

    case REG_GLOBAL_CFG:
        return DCR_BAS | (s->pass_through_8259 ? 0 : GLOBAL_CFG_8259);

    case REG_VENDOR_INT_TYPE:
        return s->vitc_mcheck_border << VITC_MCHECK_SHIFT | s->vitc_crit_border;

    case REG_IPI_VP_0:
        return mpic_get_internal_vp_reg(s, IPI_0_INDEX);

    case REG_IPI_VP_1:
        return mpic_get_internal_vp_reg(s, IPI_1_INDEX);

    case REG_IPI_VP_2:
        return mpic_get_internal_vp_reg(s, IPI_2_INDEX);

    case REG_IPI_VP_3:
        return mpic_get_internal_vp_reg(s, IPI_3_INDEX);

    case REG_SPV:
        return s->spv;

    case REG_TIMER_FREQ:
        return s->freq_reg;

    case REG_TIMER_COUNT_0:
        return mpic_get_timer_count(s, TIMER_0_INDEX);

    case REG_TIMER_COUNT_1:
        return mpic_get_timer_count(s, TIMER_1_INDEX);

    case REG_TIMER_COUNT_2:
        return mpic_get_timer_count(s, TIMER_2_INDEX);

    case REG_TIMER_COUNT_3:
        return mpic_get_timer_count(s, TIMER_3_INDEX);

    case REG_TIMER_BASE_0:
        return mpic_get_timer_base(s, TIMER_0_INDEX);

    case REG_TIMER_BASE_1:
        return mpic_get_timer_base(s, TIMER_1_INDEX);

    case REG_TIMER_BASE_2:
        return mpic_get_timer_base(s, TIMER_2_INDEX);

    case REG_TIMER_BASE_3:
        return mpic_get_timer_base(s, TIMER_3_INDEX);

    case REG_TIMER_VP_0:
        return mpic_get_internal_vp_reg(s, TIMER_0_INDEX);

    case REG_TIMER_VP_1:
        return mpic_get_internal_vp_reg(s, TIMER_1_INDEX);

    case REG_TIMER_VP_2:
        return mpic_get_internal_vp_reg(s, TIMER_2_INDEX);

    case REG_TIMER_VP_3:
        return mpic_get_internal_vp_reg(s, TIMER_3_INDEX);

    case REG_TIMER_DEST_0:
        return s->irq[TIMER_0_INDEX].destination;

    case REG_TIMER_DEST_1:
        return s->irq[TIMER_1_INDEX].destination;

    case REG_TIMER_DEST_2:
        return s->irq[TIMER_2_INDEX].destination;

    case REG_TIMER_DEST_3:
        return s->irq[TIMER_3_INDEX].destination;
    }

    // FIXME: this registers are per-cpu so handle it address and current CPU
    switch (dcrn & ~REG_CPU_MASK) {
    case REG_TASK_PRIO:
        return s->task_prio[cpu_index];

    case REG_WHO_AM_I:
        return 0;

    case REG_NON_CRIT_IAR:
        return mpic_return_current_irq(s, OUTPUT_NON_CRIT);

    case REG_CRIT_IAR:
        return mpic_return_current_irq(s, OUTPUT_CRIT);

    case REG_MCHECK_IAR:
        return mpic_return_current_irq(s, OUTPUT_MCHECK);
    }

    return 0;
}

static void mpic_dcr_write (void *opaque, int dcrn, uint32_t val)
{
    MpicState *s = MPIC(opaque);

    qemu_mutex_lock(&s->mutex);

    dcrn &= REGS_MASK;

    if (dcrn >= REG_EXT_START && dcrn < REG_EXT_END) {
        int id = (dcrn >> REG_EXT_ID_SHIFT) & REG_EXT_ID_MASK;

        if (dcrn & REG_DST_MASK) {
            s->irq[id].destination = val;
        } else {
            s->irq[id].vector   = val >> VP_VECTOR_SHIFT;
            s->irq[id].priority = val >> VP_PRIORITY_SHIFT;
            s->irq[id].sense    = val >> VP_SENSE_SHIFT;
            s->irq[id].polarity = val >> VP_POLARITY_SHIFT;
            s->irq[id].masked   = val >> VP_MASK_SHIFT;
        }
        goto end;
    }

    // parse here addrs like 0x01000 and so on
    switch (dcrn) {
    case REG_GLOBAL_CFG:
        s->pass_through_8259 = val & GLOBAL_CFG_8259 ? false : true;

        if (val & GLOBAL_CFG_RESET) {
            mpic_reset(s);
        }
        goto end;

    case REG_VENDOR_INT_TYPE:
        s->vitc_crit_border = val & VITC_BORDER_MASK;
        s->vitc_mcheck_border = (val >> VITC_MCHECK_SHIFT) & VITC_BORDER_MASK;
        goto end;

    // Processor Initialization Register (PINI) ??

    case REG_IPI_VP_0:
        mpic_set_internal_vp_reg(s, IPI_0_INDEX, val);
        goto end;

    case REG_IPI_VP_1:
        mpic_set_internal_vp_reg(s, IPI_1_INDEX, val);
        goto end;

    case REG_IPI_VP_2:
        mpic_set_internal_vp_reg(s, IPI_2_INDEX, val);
        goto end;

    case REG_IPI_VP_3:
        mpic_set_internal_vp_reg(s, IPI_3_INDEX, val);
        goto end;

    case REG_SPV:
        s->spv = val & SPV_VECTOR_MASK;
        goto end;

    case REG_TIMER_FREQ:
        s->freq_reg = val;
        goto end;

    case REG_TIMER_BASE_0:
        mpic_set_timer_base(s, TIMER_0_INDEX, val);
        goto end;

    case REG_TIMER_BASE_1:
        mpic_set_timer_base(s, TIMER_1_INDEX, val);
        goto end;

    case REG_TIMER_BASE_2:
        mpic_set_timer_base(s, TIMER_2_INDEX, val);
        goto end;

    case REG_TIMER_BASE_3:
        mpic_set_timer_base(s, TIMER_3_INDEX, val);
        goto end;

    case REG_TIMER_VP_0:
        mpic_set_internal_vp_reg(s, TIMER_0_INDEX, val);
        goto end;

    case REG_TIMER_VP_1:
        mpic_set_internal_vp_reg(s, TIMER_1_INDEX, val);
        goto end;

    case REG_TIMER_VP_2:
        mpic_set_internal_vp_reg(s, TIMER_2_INDEX, val);
        goto end;

    case REG_TIMER_VP_3:
        mpic_set_internal_vp_reg(s, TIMER_3_INDEX, val);
        goto end;

    case REG_TIMER_DEST_0:
        s->irq[TIMER_0_INDEX].destination = val;
        goto end;

    case REG_TIMER_DEST_1:
        s->irq[TIMER_1_INDEX].destination = val;
        goto end;

    case REG_TIMER_DEST_2:
        s->irq[TIMER_2_INDEX].destination = val;
        goto end;

    case REG_TIMER_DEST_3:
        s->irq[TIMER_3_INDEX].destination = val;
        goto end;

    }

    // FIXME: this registers are per-cpu so handle it address and current CPU
    irq_config_t *current_irq;
    switch (dcrn & ~REG_CPU_MASK) {
    case REG_IPID_0:
    case REG_IPID_1:
    case REG_IPID_2:
    case REG_IPID_3:
        // generate inter-cpu irq's
        s->irq[IPI_0_INDEX].pending = true;
        break;

    case REG_TASK_PRIO:
        s->task_prio[cpu_index] = val & TASK_PRIO_MASK;
        break;

    case REG_NON_CRIT_EOI:
        current_irq = mpic_remove_current_irq(s, OUTPUT_NON_CRIT);
        current_irq->activity = false;
        break;

    case REG_CRIT_EOI:
        current_irq = mpic_remove_current_irq(s, OUTPUT_CRIT);
        current_irq->activity = false;
        break;

    case REG_MCHECK_EOI:
        current_irq = mpic_remove_current_irq(s, OUTPUT_MCHECK);
        current_irq->activity = false;
        break;
    }

end:
    qemu_mutex_unlock(&s->mutex);
    mpic_update_irq(s);
}

static void mpic_input_irq(void *opaque, int n, int level)
{
    MpicState *s = MPIC(opaque);

    qemu_mutex_lock(&s->mutex);
    s->irq[n].pending = level;
    qemu_mutex_unlock(&s->mutex);

    mpic_update_irq(s);
}

static void mpic_timer_expired(void *opaque)
{
    mpic_timer_t *timer = opaque;
    MpicState *s = timer->state;

    qemu_mutex_lock(&s->mutex);

    timer->toggle_bit = timer->toggle_bit ? 0 : 1;
    s->irq[TIMER_0_INDEX + timer->id].pending = true;

    if (timer->active) {
        timer_mod(&s->qemu_timer[timer->id], qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                  muldiv64(timer->count, NANOSECONDS_PER_SECOND, s->timer_freq));
    }

    qemu_mutex_unlock(&s->mutex);

    mpic_update_irq(s);
}

static void mpic_device_realize(DeviceState *dev, Error **errp)
{
    MpicState *s = MPIC(dev);
    PowerPCCPU *cpu = POWERPC_CPU(s->cpu);
    CPUPPCState *env = &cpu->env;
    uint32_t base = s->baseaddr;
    int i;

    for (i = 0; i < MAX_TIMER_NUM; i++) {
        s->timer_data[i].state = s;
        s->timer_data[i].id = i;
        timer_init_ns(&s->qemu_timer[i], QEMU_CLOCK_VIRTUAL, mpic_timer_expired,
                      &s->timer_data[i]);
    }

    qemu_mutex_init(&s->mutex);

    qdev_init_gpio_in_named_with_opaque(dev, mpic_input_irq, s, NULL, EXT_SOURCE_NUM);

    qdev_init_gpio_out_named(dev, &s->output_irq[OUTPUT_NON_CRIT], "non_crit_int", 1);
    qdev_init_gpio_out_named(dev, &s->output_irq[OUTPUT_CRIT], "crit_int", 1);
    qdev_init_gpio_out_named(dev, &s->output_irq[OUTPUT_MCHECK], "machine_check", 1);

    for (i = REG_EXT_START; i <= REG_EXT_START + EXT_SOURCE_NUM * 0x20; i += 0x20) {
        ppc_dcr_register(env, base + i, s, mpic_dcr_read, mpic_dcr_write);
        ppc_dcr_register(env, base + i + REG_DST_MASK, s, mpic_dcr_read, mpic_dcr_write);
    }

    for (i = REG_IPID_0; i <= REG_MCHECK_EOI; i += 0x10) {
        ppc_dcr_register(env, base + i, s, mpic_dcr_read, mpic_dcr_write);
        ppc_dcr_register(env, base + REG_CPU0_OFFSET + i, s, mpic_dcr_read, mpic_dcr_write);
        ppc_dcr_register(env, base + REG_CPU1_OFFSET + i, s, mpic_dcr_read, mpic_dcr_write);
        ppc_dcr_register(env, base + REG_CPU2_OFFSET + i, s, mpic_dcr_read, mpic_dcr_write);
        ppc_dcr_register(env, base + REG_CPU3_OFFSET + i, s, mpic_dcr_read, mpic_dcr_write);
    }

    ppc_dcr_register(env, base + REG_FEATURE_REP, s, mpic_dcr_read, mpic_dcr_write);
    ppc_dcr_register(env, base + REG_GLOBAL_CFG, s, mpic_dcr_read, mpic_dcr_write);
    ppc_dcr_register(env, base + REG_VENDOR_INT_TYPE, s, mpic_dcr_read, mpic_dcr_write);
    ppc_dcr_register(env, base + REG_RAW_INT_DEBUG, s, mpic_dcr_read, mpic_dcr_write);
    ppc_dcr_register(env, base + REG_SOFT_CORE_REV, s, mpic_dcr_read, mpic_dcr_write);
    ppc_dcr_register(env, base + REG_VENDOR_ID, s, mpic_dcr_read, mpic_dcr_write);
    ppc_dcr_register(env, base + REG_PINI, s, mpic_dcr_read, mpic_dcr_write);
    ppc_dcr_register(env, base + REG_IPI_VP_0, s, mpic_dcr_read, mpic_dcr_write);
    ppc_dcr_register(env, base + REG_IPI_VP_1, s, mpic_dcr_read, mpic_dcr_write);
    ppc_dcr_register(env, base + REG_IPI_VP_2, s, mpic_dcr_read, mpic_dcr_write);
    ppc_dcr_register(env, base + REG_IPI_VP_3, s, mpic_dcr_read, mpic_dcr_write);
    ppc_dcr_register(env, base + REG_SPV, s, mpic_dcr_read, mpic_dcr_write);
    
    ppc_dcr_register(env, base + REG_TIMER_FREQ, s, mpic_dcr_read, mpic_dcr_write);
    for (i = 0x00; i <= 0x40; i += 0x10) {
        ppc_dcr_register(env, base + 0x1100 + i, s, mpic_dcr_read, mpic_dcr_write);
        ppc_dcr_register(env, base + 0x1140 + i, s, mpic_dcr_read, mpic_dcr_write);
        ppc_dcr_register(env, base + 0x1180 + i, s, mpic_dcr_read, mpic_dcr_write);
        ppc_dcr_register(env, base + 0x11c0 + i, s, mpic_dcr_read, mpic_dcr_write);
    }
}

static void mpic_device_reset(DeviceState *dev)
{
    MpicState *s = MPIC(dev);

    mpic_reset(s);
}

static Property mpic_device_properties[] = {
    DEFINE_PROP_LINK("cpu-state", MpicState, cpu, TYPE_CPU, CPUState *),
    DEFINE_PROP_UINT32("baseaddr", MpicState, baseaddr, 0xffc00000),
    DEFINE_PROP_UINT32("timer-freq", MpicState, timer_freq, 100*1000*1000),
    DEFINE_PROP_END_OF_LIST(),
};

static void mpic_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = mpic_device_reset;
    dc->realize = mpic_device_realize;
    device_class_set_props(dc, mpic_device_properties);
}

static const TypeInfo mpic_device_info = {
    .name          = TYPE_MPIC,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(MpicState),
    .class_init    = mpic_device_class_init,
};

static void mpic_register_types(void)
{
    type_register_static(&mpic_device_info);
}

type_init(mpic_register_types)
