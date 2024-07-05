/*
 * Aeroflex Gaisler GR1553B - MIL-STD-1553B / AS15531 Controller
 *
 */

#include "qemu/osdep.h"
#include "qemu/atomic.h"
#include "hw/irq.h"

#include "exec/address-spaces.h"
#include "sysemu/dma.h"

#include "hw/misc/gr1553b.h"

#include <virtmko.h>

#define REG_IRQ 0x0
#define REG_IRQ_ENABLE 0x4
#define REG_HW_CONFIG 0x10

#define REG_BC_STATUS_CONFIG 0x40
#define REG_BC_ACTION 0x44
#define REG_BC_TRANS_LIST_PTR 0x48
#define REG_BC_ASYNC_LIST_PTR 0x4c
#define REG_BC_TIMER 0x50
#define REG_BC_TIMER_WAKE_UP 0x54
#define REG_BC_IRQ_RING_POS 0x58
#define REG_BC_BUS_SWAP 0x5c
#define REG_BC_TRANS_LIST_CURR_PTR 0x68
#define REG_BC_TRANS_ASYNC_CURR_PTR 0x6c

#define IRQ_BCEV (1 << 0)

#define BC_STAT_CFG_BCSUP (1 << 31)
/* controller supports all bc features */
#define BC_STAT_CFG_BCFEAT (0x7 << 28)
#define BC_STAT_CFG_BCCHK (1 << 16)
#define BC_STAT_CFG_SCADL_MASK 0x1f
#define BC_STAT_CFG_SCADL_OFF 3
#define BC_STAT_CFG_SCST_MASK 0x7
#define BC_STAT_CFG_SCST_OFF 0

#define BC_ACT_SCHED_STOP (1 << 2)
#define BC_ACT_SCHED_SUSPEND (1 << 1)
#define BC_ACT_SCHED_START (1 << 0)

#define WRITE_KEY_MASK 0xffff0000
#define BC_KEY 0x15520000

/*
 * DMA logic
 */
typedef union {
    struct {
        uint32_t stime : 16;
        uint32_t : 2;
        uint32_t gap : 1;
        uint32_t stbus : 1;
        uint32_t nret : 3;
        uint32_t retmd : 2;
        uint32_t susn : 1;
        uint32_t suse : 1;
        uint32_t irqn : 1;
        uint32_t irqe : 1;
        uint32_t excl : 1;
        uint32_t wtrig : 1;
        uint32_t : 1;
    };
    uint32_t val;
} bc_word0_t;

typedef union {
    struct {
        uint32_t wcmc : 5;
        uint32_t rtsa1 : 5;
        uint32_t tr : 1;
        uint32_t rtad1 : 5;
        uint32_t rtsa2 : 5;
        uint32_t rtad2 : 5;
        uint32_t rtto : 4;
        uint32_t bus : 1;
        uint32_t dum : 1;
    };
    uint32_t val;
} bc_word1_t;

typedef union {
    struct {
        uint32_t tfrst : 3;
        uint32_t : 1;
        uint32_t retcnt : 4;
        uint32_t rtst : 8;
        uint32_t rt2st : 8;
        uint32_t : 8;
    };
    uint32_t val;
} bc_result_t;

typedef union {
    struct {
        uint32_t stcc : 8;
        uint32_t rtcc : 8;
        uint32_t rt2cc : 8;
        uint32_t mode : 1;
        uint32_t act : 1;
        uint32_t irqc : 1;
        uint32_t : 5;
    };
    uint32_t val;
} bc_branch_cond_t;

typedef union {
    struct {
        uint32_t is_branch_desc : 1;
        uint32_t : 31;
    };
    struct {
        bc_word0_t word0;
        bc_word1_t word1;
        uint32_t addr;
        bc_result_t result;
    };
    struct {
        bc_branch_cond_t condition;
        uint32_t jump_addr;
    };
} bc_trans_desc_t;

static int read_bc_trans_desc(AddressSpace *as, dma_addr_t addr, bc_trans_desc_t *desc)
{
    if (dma_memory_read(as, addr, desc, sizeof(bc_trans_desc_t),
                        MEMTXATTRS_UNSPECIFIED)) {
        return -1;
    }

    desc->word0.val = cpu_to_be32(desc->word0.val);
    desc->word1.val = cpu_to_be32(desc->word1.val);
    desc->addr = cpu_to_be32(desc->addr);
    return 0;
}

static int write_bc_trans_desc(AddressSpace *as, dma_addr_t addr, bc_trans_desc_t *desc)
{
    desc->result.val = be32_to_cpu(desc->result.val);

    if (dma_memory_write(as, addr + offsetof(bc_trans_desc_t, result), &desc->result,
                         sizeof(uint32_t), MEMTXATTRS_UNSPECIFIED)) {
        return -1;
    }
    return 0;
}

/*
 * controller logic
 */
enum {
    BC_SCHED_STOPPED,
    BC_SCHED_EXECUTING,
    BC_SCHED_WAITING_TIME_SLOT,
    BC_SCHED_SUSPENDED,
    BC_SCHED_WAITING_EXTERN_TRIG,
};

enum {
    INTERNAL_SIGNAL_NONE,
    INTERNAL_SIGNAL_SUSPEND = 0b01,
    INTERNAL_SIGNAL_STOP = 0b11,
};

enum {
    BC_TFRST_SUCCESS,
    BC_TFRST_RT_NO_REPSONSE,
    BC_TFRST_SECOND_RT_NO_REPSONSE,
    BC_TFRST_RT_RESPONSE_HAD_ERROR,
    BC_TFRST_PROTOCOL_ERROR,
    BC_TFRST_DESC_INVALID,
    BC_TFRST_DMA_ERROR,
    BC_TFRST_LOOPBACK_FAIL,
};

static void gr1553b_update_irq(GR1553BState *s)
{
    if (qatomic_read(&s->reg_irq) & s->reg_mask) {
        qemu_irq_raise(s->irq);
    } else {
        qemu_irq_lower(s->irq);
    }
}

static int get_format(bc_word1_t word1)
{
    // broadcast formats (7, 8, 9, 10)
    if (word1.rtad1 == 31) {
        if (word1.rtsa1 == 0 || word1.rtsa1 == 31) {
            if (word1.tr) {
                return 9;
            }

            return 10;
        }

        if (word1.rtsa2) {
            return 8;
        }

        return 7;
    }

    // command formats (4, 5, 6)
    if (word1.rtsa1 == 0 || word1.rtsa1 == 31) {
        switch (word1.wcmc) {
        case 0 ... 8:
            return 4;

        case 16:
        case 18:
        case 19:
            return 5;

        case 17:
        case 20:
        case 21:
            return 6;

        default:
            g_assert_not_reached();
        }
    }

    // message formats (1, 2, 3)
    if (word1.rtsa2 != 0) {
        return 3;
    }

    return word1.tr ? 2 : 1;
}

static void bc_send_msg(GR1553BState *s, bc_trans_desc_t *desc)
{
    desc->result.val = 0;

    vmko_msg msg;
    msg.nwords = desc->word1.wcmc;
    msg.subaddr = desc->word1.rtsa1;
    msg.transmit = desc->word1.tr;
    msg.addr = desc->word1.rtad1;
    msg.format = get_format(desc->word1);

    uint32_t size = (msg.nwords ? msg.nwords : 32) * sizeof(uint16_t);
    switch (msg.format) {
    case 1:
        if (dma_memory_read(&address_space_memory, desc->addr, msg.data, size,
                            MEMTXATTRS_UNSPECIFIED)) {
            /* FIXME: qatomic_or(&s->reg_irq, IRQ_BCD); irq and then what?*/
            g_assert_not_reached();
        }
        vmko_send(s->vmko_controller, &msg);
        if (vmko_receive(s->vmko_controller, &msg) < 0) {
            desc->result.tfrst = BC_TFRST_RT_NO_REPSONSE;
        }
        break;

    case 2:
        vmko_send(s->vmko_controller, &msg);
        if (vmko_receive(s->vmko_controller, &msg) < 0) {
            desc->result.tfrst = BC_TFRST_RT_NO_REPSONSE;
        } else {
            if (dma_memory_write(&address_space_memory, desc->addr, msg.data, size,
                                 MEMTXATTRS_UNSPECIFIED)) {
                /* FIXME: qatomic_or(&s->reg_irq, IRQ_BCD); irq and then what?*/
                g_assert_not_reached();
            }
        }
        break;

    default:
        g_assert_not_reached();
    }
}

static void exec_msg_desc(GR1553BState *s, bc_trans_desc_t *desc)
{
    /* this bits are not supported yet */
    assert(desc->word0.wtrig == 0);
    assert(desc->word0.retmd == 0);
    assert(desc->word0.nret == 0);
    assert(desc->word0.stbus == 0);
    assert(desc->word0.gap == 0);

    if (desc->word1.dum) {
        desc->result.val = 0;
        desc->result.tfrst = BC_TFRST_SUCCESS;
    } else {
        bc_send_msg(s, desc);
    }

    if (desc->result.tfrst) {
        if (desc->word0.irqe) {
            qatomic_or(&s->reg_irq, IRQ_BCEV);
            gr1553b_update_irq(s);
        }

        if (desc->word0.suse) {
            qemu_mutex_lock(&s->internal_mutex);
            s->internal_signal |= INTERNAL_SIGNAL_SUSPEND;
            qemu_mutex_unlock(&s->internal_mutex);
        }
    } else {
        if (desc->word0.irqn) {
            qatomic_or(&s->reg_irq, IRQ_BCEV);
            gr1553b_update_irq(s);
        }

        if (desc->word0.susn) {
            qemu_mutex_lock(&s->internal_mutex);
            s->internal_signal |= INTERNAL_SIGNAL_SUSPEND;
            qemu_mutex_unlock(&s->internal_mutex);
        }
    }
}

static uint32_t exec_branch_desc(GR1553BState *s, bc_trans_desc_t *desc,
                                 uint32_t curr_addr, bc_result_t prev_res)
{
    uint32_t condition;
    /* default is just next address */
    uint32_t next_addr = curr_addr + sizeof(bc_trans_desc_t);

    /* FIXME: is this correct calculations? and what is the `result`? */
    if (desc->condition.mode) {
        /* AND mode:
         * - STCC != 0x0
         * - all bits set in RT2CC,RTCC are set in RT2ST,RTST
         * - result is in STCC mask
         */
        condition = desc->condition.stcc != 0x0 &&
                    (prev_res.rtst & desc->condition.rtcc) == desc->condition.rtcc &&
                    (prev_res.rt2st & desc->condition.rt2cc) == desc->condition.rt2cc &&
                    (prev_res.rtst & desc->condition.stcc) == prev_res.rtst;
    } else {
        /* OR mode:
         * - STCC == 0xFF
         * - any bit set in RT2CC,RTCC is set in RT2ST,RTST
         * - result is in STCC mask
         */
        condition = desc->condition.stcc == 0xff ||
                    prev_res.rtst & desc->condition.rtcc ||
                    prev_res.rt2st & desc->condition.rt2cc ||
                    (prev_res.rtst & desc->condition.stcc) == prev_res.rtst;
    }

    if (!condition) {
        return next_addr;
    }

    if (desc->condition.irqc) {
        qatomic_or(&s->reg_irq, IRQ_BCEV);
        gr1553b_update_irq(s);
    }

    if (desc->condition.act) {
        /* jump to new address */
        next_addr = desc->jump_addr;
    } else {
        qemu_mutex_lock(&s->internal_mutex);
        s->internal_signal |= INTERNAL_SIGNAL_SUSPEND;
        qemu_mutex_unlock(&s->internal_mutex);
    }

    return next_addr;
}

static void *gr1553b_bc_thread(void *opaque)
{
    GR1553BState *s = GR1553B(opaque);
    bc_trans_desc_t bc_desc;
    bc_result_t prev_res = { .val = 0 };

    while (true) {
        qemu_mutex_lock(&s->bc_mutex);

        qemu_mutex_lock(&s->internal_mutex);
        /* INFO: there is a possibility that we can get here after bc_mutex was unlocked
         * but before we update s->bc_scst here it can be readed again */
        s->bc_scst = BC_SCHED_EXECUTING;
        qemu_mutex_unlock(&s->internal_mutex);

        int executing = true;
        while (executing) {
            uint32_t curr_addr = s->reg_bc_trans;

            if (read_bc_trans_desc(&address_space_memory, curr_addr, &bc_desc)) {
                /* FIXME: qatomic_or(&s->reg_irq, IRQ_BCD); irq and then what?*/
                g_assert_not_reached();
            }

            uint32_t next_addr;
            if (bc_desc.is_branch_desc) {
                next_addr = exec_branch_desc(s, &bc_desc, curr_addr, prev_res);
            } else {
                exec_msg_desc(s, &bc_desc);
                prev_res.val = bc_desc.result.val;
                if (write_bc_trans_desc(&address_space_memory, curr_addr, &bc_desc)) {
                    /* FIXME: qatomic_or(&s->reg_irq, IRQ_BCD); irq and then what?*/
                    g_assert_not_reached();
                }
                next_addr = curr_addr + sizeof(bc_trans_desc_t);
            }

            if (s->reg_bc_trans == curr_addr) {
                s->reg_bc_trans = next_addr;
            }

            qemu_mutex_lock(&s->internal_mutex);
            if (s->internal_signal) {
                s->bc_scst = s->internal_signal == INTERNAL_SIGNAL_SUSPEND ?
                    BC_SCHED_SUSPENDED : BC_SCHED_STOPPED;
                s->internal_signal = INTERNAL_SIGNAL_NONE;
                executing = false;
            }
            qemu_mutex_unlock(&s->internal_mutex);
        }
    }

    return NULL;
}

static uint64_t gr1553b_read(void *opaque, hwaddr offset, unsigned size)
{
    GR1553BState *s = GR1553B(opaque);
    uint64_t val = 0;

    switch (offset) {
    case REG_IRQ:
        val = qatomic_read(&s->reg_irq);
        break;

    case REG_IRQ_ENABLE:
        val = s->reg_mask;
        break;

    case REG_BC_STATUS_CONFIG:
        val = BC_STAT_CFG_BCSUP | BC_STAT_CFG_BCFEAT |
              (s->bc_scst & BC_STAT_CFG_SCST_MASK) << BC_STAT_CFG_SCST_OFF;
        break;

    case REG_BC_TRANS_LIST_PTR:
        val = s->reg_bc_trans;
        break;
    }

    return val;
}

static void gr1553b_write(void *opaque, hwaddr offset, uint64_t val, unsigned size)
{
    GR1553BState *s = GR1553B(opaque);

    switch (offset) {
    case REG_IRQ:
        qatomic_and(&s->reg_irq, ~val);
        break;

    case REG_IRQ_ENABLE:
        s->reg_mask = val;
        break;

    case REG_BC_ACTION:
        if ((val & WRITE_KEY_MASK) != BC_KEY) {
            break;
        }

        qemu_mutex_lock(&s->internal_mutex);
        if (val & BC_ACT_SCHED_STOP) {
            if (s->bc_scst == BC_SCHED_STOPPED || s->bc_scst == BC_SCHED_SUSPENDED) {
                s->bc_scst = BC_SCHED_STOPPED;
            } else {
                s->internal_signal |= INTERNAL_SIGNAL_STOP;
            }
        } else if (val & BC_ACT_SCHED_SUSPEND) {
            if (s->bc_scst == BC_SCHED_STOPPED || s->bc_scst == BC_SCHED_SUSPENDED) {
                s->bc_scst = BC_SCHED_SUSPENDED;
            } else {
                s->internal_signal |= INTERNAL_SIGNAL_SUSPEND;
            }
        } else if (val & BC_ACT_SCHED_START) {
            if (s->bc_scst == BC_SCHED_STOPPED || s->bc_scst == BC_SCHED_SUSPENDED) {
                qemu_mutex_unlock(&s->bc_mutex);
            }
        }
        qemu_mutex_unlock(&s->internal_mutex);
        break;

    case REG_BC_TRANS_LIST_PTR:
        s->reg_bc_trans = val;
        break;
    }

    gr1553b_update_irq(s);
}

static void gr1553b_reset(DeviceState *dev)
{
    GR1553BState *s = GR1553B(dev);

    /* TODO: how to reset bc thread? */

    qatomic_set(&s->reg_irq, 0);
    s->reg_mask = 0;

    s->reg_bc_trans = 0;
    s->bc_scst = BC_SCHED_STOPPED;
    s->internal_signal = INTERNAL_SIGNAL_NONE;

    gr1553b_update_irq(s);
}

static const MemoryRegionOps gr1553b_ops = {
    .read = gr1553b_read,
    .write = gr1553b_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void gr1553b_realize(DeviceState *dev, Error **errp)
{
    GR1553BState *s = GR1553B(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->iomem, OBJECT(dev), &gr1553b_ops, s, "gr1553b", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    /* internal */
    qemu_mutex_init(&s->internal_mutex);

    /* virtmko */
    s->vmko_controller = vmko_new();
    vmko_set_ip_port(s->vmko_controller, "224.5.0.141:3800");
    vmko_set_timeout(s->vmko_controller, 1);
    vmko_start(s->vmko_controller);

    /* create locked mutex and recv/send thread for bc mode */
    qemu_mutex_init(&s->bc_mutex);
    qemu_mutex_lock(&s->bc_mutex);
    qemu_thread_create(&s->bc_thread, "gr1553b_bc_thread", gr1553b_bc_thread, s,
                       QEMU_THREAD_JOINABLE);
}

static void gr1553b_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
    dc->desc = "Aeroflex Gaisler GR1553B Controller";
    dc->realize = gr1553b_realize;
    dc->reset = gr1553b_reset;
}

static const TypeInfo gr1553b_info = {
    .name = TYPE_GR1553B,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(GR1553BState),
    .class_init = gr1553b_class_init,
};

static void gr1553b_register_type(void)
{
    type_register_static(&gr1553b_info);
}

type_init(gr1553b_register_type)
