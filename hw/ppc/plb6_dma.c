#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "cpu.h"
#include "hw/ppc/ppc.h"
#include "hw/ppc/plb6_dma.h"
#include "hw/irq.h"
#include "sysemu/dma.h"
#include "hw/hw.h"

#define REGS_MASK 0x7F

#define CHANNEL_ONE 0x00
#define CHANNEL_TWO 0x10
#define CHANNEL_THREE 0x20
#define CHANNEL_FOUR 0x30

#define REG_CHANNEL_CTRL 0x00
#define REG_CHANNEL_CNT 0x01
#define REG_CHANNEL_SRC_ADDR_H 0x03
#define REG_CHANNEL_SRC_ADDR_L 0x04
#define REG_CHANNEL_DST_ADDR_H 0x05
#define REG_CHANNEL_DST_ADDR_L 0x06

#define REG_STATUS 0x40
#define REG_PLB_ARBITER_MODE 0x48

#define REG_STATUS_MUTABLE_BITS_MASK 0xF8888000
#define REG_STATUS_CS_ACCESS(ch_number) (1 << (31 - (ch_number)))
#define REG_STATUS_ERROR_ACCESS(ch_number) (1 << (27 - (ch_number) * 4))

#define REG_PLB_ARBITER_MODE_MASK 0xC0000000

#define GET_CURRENT_REGISTER(addr) ((addr) & 0xF)
#define GET_CURRENT_CHANNEL(addr) (((addr) >> 0x4) & 0x3)


static uint8_t is_terminal_count_int_enable(PLB6DMAState *s, uint8_t channel_num) {
    if (s->channels[channel_num].ctrl_reg.terminal_count &&
        s->channels[channel_num].ctrl_reg.terminal_count_interrupt &&
       (s->status_reg.reg_value & REG_STATUS_CS_ACCESS(channel_num))) {
        return 1;
    }
    return 0;
}

static uint8_t is_error_int_enable(PLB6DMAState *s, uint8_t channel_num) {
    if ((s->status_reg.reg_value & REG_STATUS_ERROR_ACCESS(channel_num)) &&
         s->channels[channel_num].ctrl_reg.error_interrupt) {
        return 1;
    }
    return 0;
}

static void PLB6DMA_update_irq(PLB6DMAState *s, uint8_t channel_num) {
    if (is_terminal_count_int_enable(s, channel_num) ||
        is_error_int_enable(s, channel_num)) {
        qemu_irq_raise(s->irqs[channel_num]);
    } else {
        qemu_irq_lower(s->irqs[channel_num]);
    }
}

static void set_status_reg_err(PLB6DMAState *s, uint8_t channel_num) {
    switch (channel_num) {
    case 0:
        s->status_reg.channel0_error_enable = 1;
        break;
    case 1:
        s->status_reg.channel1_error_enable = 1;
        break;
    case 2:
        s->status_reg.channel2_error_enable = 1;
        break;
    case 3:
        s->status_reg.channel3_error_enable = 1;
        break;
    }
}

/*TODO: при реализации специфичных ошибок, каждый блоков if
  следующих двух функций использовать для выставления этих ошибок*/
static uint8_t dma_plb6_transmit(PLB6DMAState *s, uint8_t channel_num) {
    uint64_t transmit_size_mutable =
        s->channels[channel_num].count_reg *
        (1 << s->channels[channel_num].ctrl_reg.transfer_width);
    uint64_t transmit_size_primary = transmit_size_mutable;

    if (!dma_memory_valid(&address_space_memory,
                          s->channels[channel_num].src_addr_reg,
                          transmit_size_mutable, DMA_DIRECTION_FROM_DEVICE,
                          MEMTXATTRS_UNSPECIFIED) ||
        !dma_memory_valid(&address_space_memory,
                          s->channels[channel_num].dst_addr_reg,
                          transmit_size_mutable, DMA_DIRECTION_TO_DEVICE,
                          MEMTXATTRS_UNSPECIFIED)) {
        return 0;
    }

    uint8_t *buf = dma_memory_map(
        &address_space_memory, s->channels[channel_num].src_addr_reg,
        &transmit_size_mutable, DMA_DIRECTION_FROM_DEVICE,
        MEMTXATTRS_UNSPECIFIED);

    if ((buf == NULL) || (transmit_size_mutable != transmit_size_primary)) {
        return 0;
    }

    if (dma_memory_write(
            &address_space_memory, s->channels[channel_num].dst_addr_reg, buf,
            transmit_size_mutable, MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
        return 0;
    }
    dma_memory_unmap(&address_space_memory, buf, transmit_size_mutable,
                     DMA_DIRECTION_FROM_DEVICE, transmit_size_mutable);

    s->channels[channel_num].src_addr_reg += transmit_size_mutable;
    s->channels[channel_num].dst_addr_reg += transmit_size_mutable;
    s->channels[channel_num].count_reg = 0;

    return 1;
}

static uint8_t can_transmit(PLB6DMAState *s, uint8_t channel_num) {
    if (s->channels[channel_num].src_addr_reg %
            (1 << (s->channels[channel_num].ctrl_reg.transfer_width)) ||
        s->channels[channel_num].dst_addr_reg %
            (1 << (s->channels[channel_num].ctrl_reg.transfer_width))) {
        return 0;
    }
    return 1;
}

static uint32_t plb6_dma_read(void *opaque, int dcrn) {
    PLB6DMAState *s = PLB6_DMA(opaque);
    uint32_t val = 0;

    dcrn &= REGS_MASK;

    if (dcrn < REG_STATUS) {
        uint8_t current_reg = GET_CURRENT_REGISTER(dcrn);
        uint8_t current_channel_num = GET_CURRENT_CHANNEL(dcrn);

        switch (current_reg) {
        case REG_CHANNEL_CTRL:
            val = s->channels[current_channel_num].ctrl_reg.reg_value;
            break;
        case REG_CHANNEL_CNT:
            val = s->channels[current_channel_num].count_reg;
            break;
        case REG_CHANNEL_SRC_ADDR_H:
            val = extract64(s->channels[current_channel_num].src_addr_reg, 32,
                            32);
            break;
        case REG_CHANNEL_SRC_ADDR_L:
            val =
                extract64(s->channels[current_channel_num].src_addr_reg, 0, 32);
            break;
        case REG_CHANNEL_DST_ADDR_H:
            val = extract64(s->channels[current_channel_num].dst_addr_reg, 32,
                            32);
            break;
        case REG_CHANNEL_DST_ADDR_L:
            val =
                extract64(s->channels[current_channel_num].dst_addr_reg, 0, 32);
            break;
        default:
            break;
        }
        return val;
    }
    switch (dcrn) {
    case REG_STATUS:
        val = s->status_reg.reg_value;
        break;
    case REG_PLB_ARBITER_MODE:
        val = s->arbiter_mode;
        break;
    default:
        break;
    }

    return val;
}

static void plb6_dma_write(void *opaque, int dcrn, uint32_t val) {
    dcrn &= REGS_MASK;

    PLB6DMAState *s = PLB6_DMA(opaque);

    if (dcrn < REG_STATUS) {
        uint8_t current_reg = GET_CURRENT_REGISTER(dcrn);
        uint8_t current_channel_num = GET_CURRENT_CHANNEL(dcrn);

        switch (current_reg) {
        case REG_CHANNEL_CTRL:
            s->channels[current_channel_num].ctrl_reg.reg_value = val;

            assert(s->channels[current_channel_num].ctrl_reg.interrupt_mode == 0);

            if (!(s->channels[current_channel_num].ctrl_reg.channel_enable) ||
                !(s->channels[current_channel_num].ctrl_reg.terminal_count) ||
                (s->status_reg.reg_value & REG_STATUS_CS_ACCESS(current_channel_num)) ||
                (s->status_reg.reg_value & REG_STATUS_ERROR_ACCESS(current_channel_num))) {
                break;
            }

            if (can_transmit(s, current_channel_num) &&
                dma_plb6_transmit(s, current_channel_num)) {
                s->status_reg.reg_value |= REG_STATUS_CS_ACCESS(current_channel_num);
            } else {
                set_status_reg_err(s, current_channel_num);
            }
            s->channels[current_channel_num].ctrl_reg.channel_enable = 0;

            PLB6DMA_update_irq(s, current_channel_num);
            break;
        case REG_CHANNEL_CNT:
            s->channels[current_channel_num].count_reg = val & 0xFFFFF;
            break;
        case REG_CHANNEL_SRC_ADDR_H:
            s->channels[current_channel_num].src_addr_reg = deposit64(
                s->channels[current_channel_num].src_addr_reg, 32, 32, val);
            break;
        case REG_CHANNEL_SRC_ADDR_L:
            s->channels[current_channel_num].src_addr_reg = deposit64(
                s->channels[current_channel_num].src_addr_reg, 0, 32, val);
            break;
        case REG_CHANNEL_DST_ADDR_H:
            s->channels[current_channel_num].dst_addr_reg = deposit64(
                s->channels[current_channel_num].dst_addr_reg, 32, 32, val);
            break;
        case REG_CHANNEL_DST_ADDR_L:
            s->channels[current_channel_num].dst_addr_reg = deposit64(
                s->channels[current_channel_num].dst_addr_reg, 0, 32, val);
            break;
        default:
            break;
        }
        return;
    }

    switch (dcrn) {
    case REG_STATUS:
        s->status_reg.reg_value &= ~(val & REG_STATUS_MUTABLE_BITS_MASK);
        for (int i = 0; i < NUMBER_OF_CHANNELS; i++) {
            if (val & REG_STATUS_CS_ACCESS(i)) {
                PLB6DMA_update_irq(s, i);
            }
        }
        break;
    case REG_PLB_ARBITER_MODE:
        s->arbiter_mode = val & REG_PLB_ARBITER_MODE_MASK;
        break;
    default:
        break;
    }
}

static void plb6_dma_reset(DeviceState *dev) {
    PLB6DMAState *s = PLB6_DMA(dev);
    int i;

    for (i = 0; i < NUMBER_OF_CHANNELS; i++) {
        s->channels[i].ctrl_reg.reg_value = 0;
        s->channels[i].count_reg = 0;
        s->channels[i].src_addr_reg = 0;
        s->channels[i].dst_addr_reg = 0;
    }

    s->status_reg.reg_value = 0;
    s->arbiter_mode = 0;
}

static void plb6_dma_realize(DeviceState *dev, Error **errp) {
    PLB6DMAState *s = PLB6_DMA(dev);
    PowerPCCPU *cpu = POWERPC_CPU(s->cpu);
    CPUPPCState *env = &cpu->env;
    int i;
    uint32_t bases[NUMBER_OF_CHANNELS] = {
	    s->baseaddr + CHANNEL_ONE,
	    s->baseaddr + CHANNEL_TWO,
	    s->baseaddr + CHANNEL_THREE,
	    s->baseaddr + CHANNEL_FOUR,
	};

    for (i = 0; i < NUMBER_OF_CHANNELS; i++) {
        ppc_dcr_register(env, bases[i] + REG_CHANNEL_CTRL, s, plb6_dma_read,
                         plb6_dma_write);
        ppc_dcr_register(env, bases[i] + REG_CHANNEL_CNT, s, plb6_dma_read,
                         plb6_dma_write);
        ppc_dcr_register(env, bases[i] + REG_CHANNEL_SRC_ADDR_H, s,
                         plb6_dma_read, plb6_dma_write);
        ppc_dcr_register(env, bases[i] + REG_CHANNEL_SRC_ADDR_L, s,
                         plb6_dma_read, plb6_dma_write);
        ppc_dcr_register(env, bases[i] + REG_CHANNEL_DST_ADDR_H, s,
                         plb6_dma_read, plb6_dma_write);
        ppc_dcr_register(env, bases[i] + REG_CHANNEL_DST_ADDR_L, s,
                         plb6_dma_read, plb6_dma_write);
    }
    ppc_dcr_register(env, s->baseaddr + REG_STATUS, s, plb6_dma_read,
                     plb6_dma_write);
    ppc_dcr_register(env, s->baseaddr + REG_PLB_ARBITER_MODE, s, plb6_dma_read,
                     plb6_dma_write);

    for (i = 0; i < NUMBER_OF_IRQS; i++) {
        qdev_init_gpio_out(DEVICE(dev), &s->irqs[i], 1);
    }
}

static Property plb6_dma_properties[] = {
    DEFINE_PROP_LINK("cpu-state", PLB6DMAState, cpu, TYPE_CPU, CPUState *),
    DEFINE_PROP_UINT32("baseaddr", PLB6DMAState, baseaddr, 0x80000100),
    DEFINE_PROP_END_OF_LIST(),
};

static void plb6_dma_class_init(ObjectClass *oc, void *data) {
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->reset = plb6_dma_reset;
    dc->realize = plb6_dma_realize;
    dc->desc = "DMA-to-PLB6 Controller";
    device_class_set_props(dc, plb6_dma_properties);
}

static const TypeInfo plb6_dma_info = {
    .name = TYPE_PLB6_DMA,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(PLB6DMAState),
    .class_init = plb6_dma_class_init,
};

static void plb6_dma_register_types(void) {
    type_register_static(&plb6_dma_info);
}

type_init(plb6_dma_register_types);
