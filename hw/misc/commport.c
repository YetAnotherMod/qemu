#include "qemu/osdep.h"
#include "hw/misc/commport.h"
#include "chardev/char-serial.h"
#include "hw/irq.h"
#include "hw/qdev-properties-system.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "qapi/error.h"
#include "sysemu/dma.h"

#define REG_TRANSMIT_MAIN_COUNTER 0x0
#define REG_TRANSMIT_CURR_ADDRESS 0x2
#define REG_TRANSMIT_BIAS 0x4
#define REG_TRANSMIT_ROW_COUNTER 0x6
#define REG_TRANSMIT_ADDRESSING_MODE 0x8
#define REG_TRANSMIT_CSR 0xA
#define REG_TRANSMIT_INTERRUPT_MASK 0xC
#define REG_TRANSMIT_STATE 0xE

#define REG_RECEIVE_MAIN_COUNTER 0x10
#define REG_RECEIVE_CURR_ADDRESS 0x12
#define REG_RECEIVE_BIAS 0x14
#define REG_RECEIVE_ROW_COUNTER 0x16
#define REG_RECEIVE_ADDRESSING_MODE 0x18
#define REG_RECEIVE_CSR 0x1A
#define REG_RECEIVE_INTERRUPT_MASK 0x1C
#define REG_RECEIVE_STATE 0x1E

#define REG_OPTIONAL_HC 0x20
#define REG_OPTIONAL_ERR1_C 0x34
#define REG_OPTIONAL_ERR2_C 0x36

#define BIT_CSR_EN 0x1
#define BIT_CSR_CPL 0x2
#define BIT_CSR_ES 0x4
#define BIT_CSR_CLR 0x8

#define BIT_INTERNAL_STATE_IDLE 0x0
#define BIT_INTERNAL_STATE_COMPLETE (0x2 << 23)
#define BIT_INTERNAL_STATE_READ_SEND (0x1 << 23)
#define BIT_INTERNAL_STATE_RECEIVE_WRITE (0x1 << 23)

#define BIT_INTERRUPT_MASK_MIC 0x1
#define BIT_INTERRUPT_MASK_MIE 0x2

#define PACKET_LEN_STANDARD 8
#define PACKET_LEN_HAMMING (PACKET_LEN_STANDARD + 3)

#define COM_MODE_PPC 1
#define COM_MODE_NMC 2
#define DROP_LAST_3BITS(addr) (addr & ~7ull)

static void comm_update_irq(CommState *s)
{
    if ((!(s->comm_transmit_interrupt_mask & BIT_INTERRUPT_MASK_MIC) &&
         (s->comm_transmit_CSR & BIT_CSR_CPL)) ||
        (!(s->comm_transmit_interrupt_mask & BIT_INTERRUPT_MASK_MIE) &&
         (s->comm_transmit_CSR & BIT_CSR_ES))) {
        qemu_irq_raise(s->irq0);
    } else {
        qemu_irq_lower(s->irq0);
    }

    if ((!(s->comm_receive_interrupt_mask & BIT_INTERRUPT_MASK_MIC) &&
         (s->comm_receive_CSR & BIT_CSR_CPL)) ||
        (!(s->comm_receive_interrupt_mask & BIT_INTERRUPT_MASK_MIE) &&
         (s->comm_receive_CSR & BIT_CSR_ES))) {
        qemu_irq_raise(s->irq1);
    } else {
        qemu_irq_lower(s->irq1);
    }
}

static void comm_tx(CommState *s)
{
    uint8_t sizeof_part =
        (s->comm_optional_hc) ? PACKET_LEN_HAMMING : PACKET_LEN_STANDARD;
    uint8_t *buffer = malloc(sizeof_part * sizeof(uint8_t));
    uint8_t dma_read_result;

    for (int i = 0; i < s->comm_transmit_main_counter; i++) {
        dma_read_result =
            dma_memory_read(s->addr_space, s->comm_transmit_address, buffer,
                            PACKET_LEN_STANDARD, MEMTXATTRS_UNSPECIFIED);

        if (dma_read_result != MEMTX_OK) {
            s->comm_transmit_CSR = BIT_CSR_ES;
            s->comm_transmit_internal_state = BIT_INTERNAL_STATE_IDLE;
            comm_update_irq(s);
            return;
        }

        qemu_chr_fe_write_all(&s->comm_chr, buffer, sizeof_part);
        s->comm_transmit_address += PACKET_LEN_STANDARD;
    }

    s->comm_transmit_main_counter = 0;
    if ((s->comm_transmit_CSR == BIT_CSR_EN) &&
        !s->comm_transmit_main_counter) {
        s->comm_transmit_CSR = BIT_CSR_CPL;
        s->comm_transmit_internal_state = BIT_INTERNAL_STATE_COMPLETE;
        comm_update_irq(s);
    }
}

static void comm_rx(CommState *s, const uint8_t *buffer)
{
    uint8_t dma_write_result;
    dma_write_result =
        dma_memory_write(s->addr_space, s->comm_receive_address, buffer,
                         PACKET_LEN_STANDARD, MEMTXATTRS_UNSPECIFIED);

    if (dma_write_result != MEMTX_OK) {
        s->comm_receive_CSR = BIT_CSR_ES;
        s->comm_receive_internal_state = BIT_INTERNAL_STATE_IDLE;
        comm_update_irq(s);
        return;
    }

    s->comm_receive_address += PACKET_LEN_STANDARD;
    s->comm_receive_main_counter--;
    if ((s->comm_receive_CSR == BIT_CSR_EN) && !s->comm_receive_main_counter) {
        s->comm_receive_CSR = BIT_CSR_CPL;
        s->comm_receive_internal_state = BIT_INTERNAL_STATE_COMPLETE;
        comm_update_irq(s);
    }
}

static uint64_t comm_read(void *opaque, hwaddr addr, unsigned size)
{
    CommState *s = opaque;
    uint64_t val = 0;
    addr >>= s->registers_mode;

    switch (addr) {
    case REG_TRANSMIT_MAIN_COUNTER:
        val = s->comm_transmit_main_counter;
        break;
    case REG_RECEIVE_MAIN_COUNTER:
        val = s->comm_receive_main_counter;
        break;

    case REG_TRANSMIT_CURR_ADDRESS:
        val = (s->registers_mode == COM_MODE_NMC)
                  ? (s->comm_transmit_address / sizeof(nmc_byte_t))
                  : s->comm_transmit_address;
        break;
    case REG_RECEIVE_CURR_ADDRESS:
        val = (s->registers_mode == COM_MODE_NMC)
                  ? (s->comm_receive_address / sizeof(nmc_byte_t))
                  : s->comm_receive_address;
        break;

    case REG_TRANSMIT_BIAS:
        val = s->comm_transmit_bias;
        break;
    case REG_RECEIVE_BIAS:
        val = s->comm_receive_bias;
        break;

    case REG_TRANSMIT_ROW_COUNTER:
        val = s->comm_transmit_row_counter;
        break;

    case REG_RECEIVE_ROW_COUNTER:
        val = s->comm_receive_row_counter;
        break;

    case REG_TRANSMIT_ADDRESSING_MODE:
        val = s->comm_transmit_addressing_mode;
        break;
    case REG_RECEIVE_ADDRESSING_MODE:
        val = s->comm_receive_addressing_mode;
        break;

    case REG_TRANSMIT_CSR:
        val = s->comm_transmit_CSR;
        break;

    case REG_RECEIVE_CSR:
        val = s->comm_receive_CSR;
        break;

    case REG_TRANSMIT_INTERRUPT_MASK:
        val = s->comm_transmit_interrupt_mask;
        break;

    case REG_RECEIVE_INTERRUPT_MASK:
        val = s->comm_receive_interrupt_mask;
        break;

    case REG_TRANSMIT_STATE:
        val = s->comm_transmit_internal_state;
        break;

    case REG_RECEIVE_STATE:
        val = s->comm_receive_internal_state;
        break;

    case REG_OPTIONAL_HC:
        val = s->comm_optional_hc;
        break;

    case REG_OPTIONAL_ERR1_C:
        val = s->comm_optional_err1_c;
        break;
    case REG_OPTIONAL_ERR2_C:
        val = s->comm_optional_err2_c;
        break;

    default:
        break;
    }

    return val;
}

static void comm_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    CommState *s = COMM(opaque);
    addr >>= s->registers_mode;

    switch (addr) {
    case REG_TRANSMIT_MAIN_COUNTER:
        s->comm_transmit_main_counter = val;
        break;

    case REG_RECEIVE_MAIN_COUNTER:
        s->comm_receive_main_counter = val;
        break;

    case REG_TRANSMIT_CURR_ADDRESS:
        s->comm_transmit_address = DROP_LAST_3BITS(
            (s->registers_mode == COM_MODE_NMC) ? (val * sizeof(nmc_byte_t))
                                                : val);
        break;

    case REG_RECEIVE_CURR_ADDRESS:
        s->comm_receive_address = DROP_LAST_3BITS(
            (s->registers_mode == COM_MODE_NMC) ? (val * sizeof(nmc_byte_t))
                                                : val);
        break;

    case REG_TRANSMIT_BIAS:
        s->comm_transmit_bias = val;
        break;

    case REG_RECEIVE_BIAS:
        s->comm_receive_bias = val;
        break;

    case REG_TRANSMIT_ROW_COUNTER:
        s->comm_transmit_row_counter = val;
        break;

    case REG_RECEIVE_ROW_COUNTER:
        s->comm_receive_row_counter = val;
        break;

    case REG_TRANSMIT_ADDRESSING_MODE:
        s->comm_transmit_addressing_mode = val;
        break;

    case REG_RECEIVE_ADDRESSING_MODE:
        s->comm_receive_addressing_mode = val;
        break;

    case REG_TRANSMIT_CSR:
        s->comm_transmit_CSR = val;
        comm_update_irq(s);
        if (s->comm_transmit_CSR != BIT_CSR_EN) {
            break;
        }
        s->comm_transmit_internal_state = BIT_INTERNAL_STATE_READ_SEND;
        comm_tx(s);
        break;

    case REG_RECEIVE_CSR:
        s->comm_receive_CSR = val;
        comm_update_irq(s);
        if (s->comm_receive_CSR != BIT_CSR_EN) {
            break;
        }
        qemu_chr_fe_accept_input(&s->comm_chr);
        break;

    case REG_TRANSMIT_INTERRUPT_MASK:
        s->comm_transmit_interrupt_mask = val;
        comm_update_irq(s);
        break;

    case REG_RECEIVE_INTERRUPT_MASK:
        s->comm_receive_interrupt_mask = val;
        comm_update_irq(s);
        break;

    case REG_OPTIONAL_HC:
        s->comm_optional_hc = val;
        break;

    case REG_OPTIONAL_ERR1_C:
        s->comm_optional_err1_c = val;
        break;

    case REG_OPTIONAL_ERR2_C:
        s->comm_optional_err2_c = val;
        break;

    default:
        break;
    }
}

static const MemoryRegionOps comm_ops = {
    .read = comm_read,
    .write = comm_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void comm_reset(DeviceState *dev)
{
    CommState *s = COMM(dev);

    s->comm_receive_main_counter = 0;
    s->comm_receive_address = 0x0;
    s->comm_receive_bias = 0;
    s->comm_receive_row_counter = 0;
    s->comm_receive_addressing_mode = 0;
    s->comm_receive_CSR = 0;
    s->comm_receive_interrupt_mask = 0;
    s->comm_receive_internal_state = BIT_INTERNAL_STATE_IDLE;
    s->comm_transmit_main_counter = 0;
    s->comm_transmit_address = 0x0;
    s->comm_transmit_bias = 0;
    s->comm_transmit_row_counter = 0;
    s->comm_transmit_addressing_mode = 0;
    s->comm_transmit_CSR = 0;
    s->comm_transmit_interrupt_mask = 0;
    s->comm_transmit_internal_state = BIT_INTERNAL_STATE_IDLE;
    s->comm_optional_hc = 0;
    s->comm_optional_err1_c = 0;
    s->comm_optional_err2_c = 0;
}

static int comm_can_receive(void *opaque)
{
    CommState *s = opaque;

    if (s->comm_receive_CSR != BIT_CSR_EN) {
        return 0;
    }
    if (s->comm_optional_hc) {
        return PACKET_LEN_HAMMING;
    }

    return PACKET_LEN_STANDARD;
}

static void comm_receive(void *opaque, const uint8_t *data_char, int size)
{
    CommState *s = opaque;

    if (s->comm_receive_CSR == BIT_CSR_ES) {
        return;
    }
    s->comm_receive_internal_state = BIT_INTERNAL_STATE_RECEIVE_WRITE;

    comm_rx(s, data_char);
}

void comm_change_address_space(CommState *s, AddressSpace *addr_space,
                               Error **errp)
{
    if (object_property_get_bool(OBJECT(s), "realized", errp)) {
        error_setg(errp, "Can't change address_space of realized device\n");
    }

    s->addr_space = addr_space;
}

static Property comm_properties[] = {
    DEFINE_PROP_CHR("CommChardev", CommState, comm_chr),
/* RegistersMode property depending on which board or emulator is using:
COM_MODE_PPC - 1, COM_MODE_NMC - 2. */
    DEFINE_PROP_UINT8("RegistersMode", CommState, registers_mode,
                      0),
    DEFINE_PROP_END_OF_LIST(),
};

static void comm_realize(DeviceState *dev, Error **errp)
{
    CommState *s = COMM(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    memory_region_init_io(&s->iomem, OBJECT(dev), &comm_ops, s, "comm", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq0);
    sysbus_init_irq(sbd, &s->irq1);
    qemu_chr_fe_set_handlers(&s->comm_chr, comm_can_receive, comm_receive, NULL,
                             NULL, s, NULL, true);
    if (s->addr_space == NULL) {
        s->addr_space = &address_space_memory;
    }
}

static void comm_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->reset = comm_reset;
    dc->realize = comm_realize;
    dc->desc = "NMC communication port";
    device_class_set_props(dc, comm_properties);
}

static const TypeInfo comm_info = {
    .name = TYPE_COMM,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(CommState),
    .class_init = comm_class_init,
};

static void comm_register_types(void)
{
    type_register_static(&comm_info);
}

type_init(comm_register_types)
