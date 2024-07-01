/*
 * Aeroflex Gaisler GR1553B - MIL-STD-1553B / AS15531 Controller
 *
 */

#include "qemu/osdep.h"
#include "hw/irq.h"

#include "exec/address-spaces.h"
#include "sysemu/dma.h"

#include "hw/misc/gr1553b.h"

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
        uint32_t desc_type : 1;
        uint32_t : 31;
    };
    struct {
        bc_word0_t word0;
        bc_word1_t word1;
        uint32_t addr;
        uint32_t result;
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
    desc->result = be32_to_cpu(desc->result);

    if (dma_memory_write(as, addr + offsetof(bc_trans_desc_t, result), &desc->result,
                         sizeof(uint32_t), MEMTXATTRS_UNSPECIFIED)) {
        return -1;
    }
    return 0;
}

/*
 * controller logic
 */
static void gr1553b_update_irq(GR1553BState *s)
{
    if (s->reg_irq & s->reg_mask) {
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

static void exec_msg_desc(GR1553BState *s, bc_trans_desc_t *desc)
{
    assert(!desc->word0.wtrig);
    assert(!desc->word0.excl);

    switch (get_format(desc->word1)) {
    case 1:
        printf("this is format 1\n");
        break;

    case 2:
        printf("this is format 2\n");
        break;

    default:
        g_assert_not_reached();
    }
}

static bool exec_branch_desc(GR1553BState *s, bc_trans_desc_t *desc)
{
    uint32_t condition;

    if (desc->condition.mode) {
        // and mode
        // TODO: add.. this.. somehow..
        g_assert_not_reached();
    } else {
        // or mode
        // TODO: add (desc->condition.rt2cc & rt2st (where to get it?))
        // TODO: add (desc->condition.rtcc & rtst (where to get it?))
        condition = desc->condition.stcc;
    }

    if (!condition) {
        return false;
    }

    if (desc->condition.irqc) {
        // TODO: interrupt
        g_assert_not_reached();
    }

    if (desc->condition.act) {
        s->reg_bc_trans = desc->jump_addr;
    } else {
        s->reg_bc_act |= BC_ACT_SCHED_SUSPEND;
    }

    return true;
}

static void gr1553b_schedule_start(GR1553BState *s)
{
    bc_trans_desc_t bc_desc;

    while (1) {
        read_bc_trans_desc(&address_space_memory, s->reg_bc_trans, &bc_desc);

        if (bc_desc.desc_type) {
            if (!exec_branch_desc(s, &bc_desc)) {
                s->reg_bc_trans += sizeof(bc_trans_desc_t);
            }
        } else {
            exec_msg_desc(s, &bc_desc);

            write_bc_trans_desc(&address_space_memory, s->reg_bc_trans, &bc_desc);
            s->reg_bc_trans += sizeof(bc_trans_desc_t);
        }

        if (s->reg_bc_act & (BC_ACT_SCHED_SUSPEND | BC_ACT_SCHED_STOP)) {
            break;
        }
    }
}

static uint64_t gr1553b_read(void *opaque, hwaddr offset, unsigned size)
{
    GR1553BState *s = GR1553B(opaque);
    uint64_t val = 0;

    switch (offset) {
    case REG_IRQ:
        val = s->reg_irq;
        break;

    case REG_IRQ_ENABLE:
        val = s->reg_mask;
        break;

    case REG_BC_ACTION:
        val = s->reg_bc_act;
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
        s->reg_irq &= ~val;
        break;

    case REG_IRQ_ENABLE:
        s->reg_mask = val;
        break;

    case REG_BC_ACTION:
        if ((val & WRITE_KEY_MASK) != BC_KEY) {
            break;
        }

        s->reg_bc_act = val;

        if (s->reg_bc_act & BC_ACT_SCHED_START) {
            gr1553b_schedule_start(s);
        }
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

    s->reg_irq = 0;
    s->reg_mask = 0;

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
