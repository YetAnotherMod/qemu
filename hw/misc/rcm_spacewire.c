/*
 * RC Module spacewire controller
 *
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"

#include "hw/misc/rcm_spacewire.h"

#define SW_REG_ID 0x0
#define SW_REG_VERSION 0x4
#define SW_REG_RESET 0x8
#define SW_REG_SETTINGS 0xc
#define SW_REG_STATUS 0x10
#define SW_REG_ADMA_RESET 0x808
#define SW_REG_ADMA_CH_STATUS 0x80c

#define SW_REG_RDMA_SETTINGS 0x900
#define SW_REG_RDMA_STATUS 0x904
#define SW_REG_RDMA_SYS_ADDR 0x908
#define SW_REG_RDMA_TBL_SIZE 0x90c

#define SW_REG_WDMA_SETTINGS 0xa00
#define SW_REG_WDMA_STATUS 0xa04
#define SW_REG_WDMA_SYS_ADDR 0xa08
#define SW_REG_WDMA_TBL_SIZE 0xa0c

#define SW_ID 0x42435348u
#define SW_VERSION 0x102u

#define SW_SETTINGS_ENABLE (1u << 0)
#define SW_SETTINGS_TX_ENDIAN (1u << 1)
#define SW_SETTINGS_RX_ENDIAN (1u << 2)
#define SW_SETTINGS_LOOPBACK (1u << 5)
#define SW_SETTINGS_MASK \
    (SW_SETTINGS_ENABLE | SW_SETTINGS_TX_ENDIAN | SW_SETTINGS_RX_ENDIAN | \
     SW_SETTINGS_LOOPBACK)

#define SW_ADMA_CH_STATUS_RDMA_IRQ (1u << 0)
#define SW_ADMA_CH_STATUS_WDMA_IRQ (1u << 16)

#define SW_RWDMA_SETTINGS_DESC_INT (1u << 0)
#define SW_RWDMA_SETTINGS_ENABLE (1u << 28)
#define SW_RWDMA_SETTINGS_DESC_TBL (1u << 29)
#define SW_RWDMA_SETTINGS_LONG_LEN (1u << 30)
#define SW_RWDMA_SETTINGS_MASK \
    (SW_RWDMA_SETTINGS_DESC_INT | SW_RWDMA_SETTINGS_ENABLE | \
     SW_RWDMA_SETTINGS_DESC_TBL | SW_RWDMA_SETTINGS_LONG_LEN)

#define SW_DESC_ACTIVITY_TRAN 0x2
#define SW_DESC_ACTIVITY_COMPL 0x1

/*
 * DMA logic
 */
typedef struct {
    union {
        struct {
            uint32_t valid:1;
            uint32_t :1;
            uint32_t interrupt:1;
            uint32_t with_end:1;
            uint32_t activity:2;
            uint32_t length:26;
        };
        struct {
            uint32_t :1;
            uint32_t connection_error:1;
            uint32_t parity_error:1;
            uint32_t :29;
        };
        uint32_t cmd;
    };
    uint32_t addr;
} sw_dma_desc_t;

static int read_dma_desc(RCMSpaceWireState *s, dma_addr_t addr, sw_dma_desc_t *desc)
{
    if (dma_memory_read(s->addr_space, addr, desc, sizeof(sw_dma_desc_t),
                        MEMTXATTRS_UNSPECIFIED)) {
        return -1;
    }
    return 0;
}

static int write_dma_desc(RCMSpaceWireState *s, dma_addr_t addr, sw_dma_desc_t *desc)
{
    if (dma_memory_write(s->addr_space, addr, desc, sizeof(sw_dma_desc_t),
                         MEMTXATTRS_UNSPECIFIED)) {
        return -1;
    }
    return 0;
}

/*
 * controller logic
 */
static void rcm_sw_update_irq(RCMSpaceWireState *s)
{
    // TODO: add core_irq functionality
    qemu_irq_lower(s->core_irq);

    uint32_t rdma_irq = qatomic_read(&s->rdma_status) &
                        qatomic_read(&s->rdma_settings) &
                        SW_RWDMA_SETTINGS_DESC_INT;
    uint32_t wdma_irq = qatomic_read(&s->wdma_status) &
                        qatomic_read(&s->wdma_settings) &
                        SW_RWDMA_SETTINGS_DESC_INT;

    if (rdma_irq || wdma_irq) {
        qemu_irq_raise(s->dma_irq);
    } else {
        qemu_irq_lower(s->dma_irq);
    }
}

static void rcm_sw_soft_reset(RCMSpaceWireState *s)
{
    s->settings = 0;
    qatomic_set(&s->rdma_settings, 0);
    qatomic_set(&s->wdma_settings, 0);
    qatomic_set(&s->rdma_status, 0);
    qatomic_set(&s->wdma_status, 0);
    s->rdma_sys_addr = 0;
    s->wdma_sys_addr = 0;
    s->rdma_tbl_size = 0x800;
    s->rdma_tbl_size_internal = 0x800;
    s->wdma_tbl_size = 0x800;
    s->wdma_tbl_size_internal = 0x800;
    qatomic_set(&s->rdma_active, false);
    qatomic_set(&s->wdma_active, false);

    rcm_sw_update_irq(s);
}

static void rcm_sw_wdma_recv(RCMSpaceWireState *s)
{
    // проверить, что прием не выключен
    if (!(s->settings & SW_SETTINGS_ENABLE)) {
        goto _stop_wdma;
    }

    if (!(qatomic_read(&s->wdma_settings) & SW_RWDMA_SETTINGS_ENABLE)) {
        goto _stop_wdma;
    }

    // взять очередной дескриптор
    sw_dma_desc_t desc = {0};
    uint32_t offset = s->wdma_tbl_size_internal - s->wdma_tbl_size;

    if (read_dma_desc(s, s->wdma_sys_addr + offset, &desc)) {
        // FIXME: set error???
        g_assert_not_reached();
    }

    // завершить, если он не активный
    if (desc.activity != SW_DESC_ACTIVITY_TRAN) {
        goto _stop_wdma;
    }

    if (!desc.length) {
        // FIXME: ZERO-length packets are not supported yet
        g_assert_not_reached();
    }

    // отправить запрос, если он активный
    if (s->settings & SW_SETTINGS_LOOPBACK) {
        g_assert_not_reached();
    } else {
        s->wdma_len = desc.length;
        char *ptr = dma_memory_map(s->addr_space, desc.addr, &s->wdma_len,
                                   DMA_DIRECTION_FROM_DEVICE, MEMTXATTRS_UNSPECIFIED);
        if (!ptr) {
            /* FIXME: ?? */
            g_assert_not_reached();
        }

        sw_data data = {
            .packet_end = SW_PACKET_EOP,
            .size = desc.length,
            .data = ptr,
        };

        if (sw_logic_read_request(s->sw_logic, &data)) {
            /* FIXME: ?? */
            g_assert_not_reached();
        }
    }

    return;

_stop_wdma:
    qatomic_and(&s->wdma_settings, ~SW_RWDMA_SETTINGS_ENABLE);
    qatomic_set(&s->wdma_active, false);
}

static void rcm_sw_rdma_send(RCMSpaceWireState *s)
{
    // проверить, что передача не выключена
    if (!(s->settings & SW_SETTINGS_ENABLE)) {
        goto _stop_rdma;
    }

    if (!(qatomic_read(&s->rdma_settings) & SW_RWDMA_SETTINGS_ENABLE)) {
        goto _stop_rdma;
    }

    // взять очередной дескриптор
    sw_dma_desc_t desc = {0};
    uint32_t offset = s->rdma_tbl_size_internal - s->rdma_tbl_size;

    if (read_dma_desc(s, s->rdma_sys_addr + offset, &desc)) {
        // FIXME: set error???
        g_assert_not_reached();
    }

    // завершить, если он не активный
    if (desc.activity != SW_DESC_ACTIVITY_TRAN) {
        goto _stop_rdma;
    }

    if (!desc.length) {
        // FIXME: ZERO-length packets are not supported yet
        g_assert_not_reached();
    }

    // отправить запрос, если он активный
    if (s->settings & SW_SETTINGS_LOOPBACK) {
        g_assert_not_reached();
    } else {
        /* TODO: this bits are not supported yet */
        g_assert(desc.with_end);
        g_assert(desc.valid);

        s->rdma_len = desc.length;
        char *ptr = dma_memory_map(s->addr_space, desc.addr, &s->rdma_len,
                                   DMA_DIRECTION_FROM_DEVICE, MEMTXATTRS_UNSPECIFIED);
        if (!ptr) {
            /* FIXME: ?? */
            g_assert_not_reached();
        }

        sw_data data = {
            .packet_end = SW_PACKET_EOP,
            .size = desc.length,
            .data = ptr,
        };

        if (sw_logic_write_request(s->sw_logic, &data)) {
            /* FIXME: ?? */
            g_assert_not_reached();
        }
    }

    return;

_stop_rdma:
    qatomic_and(&s->rdma_settings, ~SW_RWDMA_SETTINGS_ENABLE);
    qatomic_set(&s->rdma_active, false);
}

static uint64_t rcm_sw_read(void *opaque, hwaddr offset, unsigned size)
{
    RCMSpaceWireState *s = RCM_SPACEWIRE(opaque);
    uint64_t val = 0;

    switch (offset) {
    case SW_REG_ID:
        val = SW_ID;
        break;

    case SW_REG_VERSION:
        val = SW_VERSION;
        break;

    /* return 0 as 'reset is done' */
    case SW_REG_RESET:
    case SW_REG_ADMA_RESET:
        break;

    case SW_REG_SETTINGS:
        val = s->settings;
        break;

    case SW_REG_ADMA_CH_STATUS: {
        uint32_t rdma_irq = qatomic_read(&s->rdma_status) &
                            qatomic_read(&s->rdma_settings) &
                            SW_RWDMA_SETTINGS_DESC_INT;
        uint32_t wdma_irq = qatomic_read(&s->wdma_status) &
                            qatomic_read(&s->wdma_settings) &
                            SW_RWDMA_SETTINGS_DESC_INT;

        val = rdma_irq ? SW_ADMA_CH_STATUS_RDMA_IRQ : 0;
        val |= wdma_irq ? SW_ADMA_CH_STATUS_WDMA_IRQ : 0;
        break;
    }

    case SW_REG_RDMA_SETTINGS:
        val = qatomic_read(&s->rdma_settings);
        break;

    case SW_REG_RDMA_STATUS:
        qemu_mutex_lock(&s->rdma_mutex);
        val = qatomic_xchg(&s->rdma_status, 0);
        qemu_mutex_unlock(&s->rdma_mutex);
        rcm_sw_update_irq(s);
        break;

    case SW_REG_WDMA_SETTINGS:
        val = qatomic_read(&s->wdma_settings);
        break;

    case SW_REG_WDMA_STATUS:
        qemu_mutex_lock(&s->wdma_mutex);
        val = qatomic_xchg(&s->wdma_status, 0);
        qemu_mutex_unlock(&s->wdma_mutex);
        rcm_sw_update_irq(s);
        break;

    case SW_REG_RDMA_SYS_ADDR:
        val = s->rdma_sys_addr;
        break;

    case SW_REG_RDMA_TBL_SIZE:
        val = s->rdma_tbl_size;
        break;

    case SW_REG_WDMA_SYS_ADDR:
        val = s->wdma_sys_addr;
        break;

    case SW_REG_WDMA_TBL_SIZE:
        val = s->wdma_tbl_size;
        break;
    }

    return val;
}

static void rcm_sw_write(void *opaque, hwaddr offset, uint64_t val, unsigned size)
{
    RCMSpaceWireState *s = RCM_SPACEWIRE(opaque);

    switch (offset) {
    case SW_REG_RESET:
        if (val == 1u) {
            rcm_sw_soft_reset(s);
        }
        break;

    case SW_REG_SETTINGS:
        qemu_mutex_lock(&s->wdma_mutex);
        qemu_mutex_lock(&s->rdma_mutex);
        s->settings = val & SW_SETTINGS_MASK;

        if (val & SW_SETTINGS_ENABLE) {
            /* start dma only when it was stopped */
            if (qatomic_xchg(&s->wdma_active, true) == false) {
                rcm_sw_wdma_recv(s);
            }

            /* start dma only when it was stopped */
            if (qatomic_xchg(&s->rdma_active, true) == false) {
                rcm_sw_rdma_send(s);
            }
        }
        qemu_mutex_unlock(&s->rdma_mutex);
        qemu_mutex_unlock(&s->wdma_mutex);
        break;

    case SW_REG_RDMA_SETTINGS:
        qemu_mutex_lock(&s->rdma_mutex);
        qatomic_or(&s->rdma_settings, SW_RWDMA_SETTINGS_MASK);

        if (val & SW_RWDMA_SETTINGS_ENABLE) {
            /* start dma only when it was stopped */
            if (qatomic_xchg(&s->rdma_active, true) == false) {
                rcm_sw_rdma_send(s);
            }
        }
        qemu_mutex_unlock(&s->rdma_mutex);
        break;

    case SW_REG_WDMA_SETTINGS:
        qemu_mutex_lock(&s->wdma_mutex);
        qatomic_or(&s->wdma_settings, SW_RWDMA_SETTINGS_MASK);

        if (val & SW_RWDMA_SETTINGS_ENABLE) {
            /* start dma only when it was stopped */
            if (qatomic_xchg(&s->wdma_active, true) == false) {
                rcm_sw_wdma_recv(s);
            }
        }
        qemu_mutex_unlock(&s->wdma_mutex);
        break;

    case SW_REG_RDMA_SYS_ADDR:
        if (qatomic_read(&s->rdma_settings) & SW_RWDMA_SETTINGS_ENABLE) {
            break;
        }

        s->rdma_sys_addr = val;
        break;

    case SW_REG_RDMA_TBL_SIZE:
        if (qatomic_read(&s->rdma_settings) & SW_RWDMA_SETTINGS_ENABLE) {
            break;
        }

        s->rdma_tbl_size = val;
        s->rdma_tbl_size_internal = val;
        break;

    case SW_REG_WDMA_SYS_ADDR:
        if (qatomic_read(&s->wdma_settings) & SW_RWDMA_SETTINGS_ENABLE) {
            break;
        }

        s->wdma_sys_addr = val;
        break;

    case SW_REG_WDMA_TBL_SIZE:
        if (qatomic_read(&s->wdma_settings) & SW_RWDMA_SETTINGS_ENABLE) {
            break;
        }

        s->wdma_tbl_size = val;
        s->wdma_tbl_size_internal = val;
        break;
    }
}

static void rcm_sw_reset(DeviceState *dev)
{
    RCMSpaceWireState *s = RCM_SPACEWIRE(dev);

    rcm_sw_soft_reset(s);
}

static const MemoryRegionOps rcm_sw_ops = {
    .read = rcm_sw_read,
    .write = rcm_sw_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

/*
 * virtsw logic
 */
static void rcm_sw_read_done(sw_data *data, void *user_data)
{
    RCMSpaceWireState *s = user_data;

    /* for now support only full and good packets */
    g_assert(data->packet_end == SW_PACKET_EOP);

    // взять текущий дескриптор
    sw_dma_desc_t desc = {0};
    uint32_t offset = s->wdma_tbl_size_internal - s->wdma_tbl_size;

    if (read_dma_desc(s, s->wdma_sys_addr + offset, &desc)) {
        // FIXME: set error???
        g_assert_not_reached();
    }

    // обработать текущий дескриптор
    bool desc_interrupt = desc.interrupt ? true : false;

    desc.activity = SW_DESC_ACTIVITY_COMPL;
    desc.length = data->size;
    desc.with_end = 1;
    desc.parity_error = 0;
    desc.connection_error = 0; //FIXME: тут был раньше sw_status: status == SW_OK ? 0 : 1;
    desc.valid = 1;

    if (write_dma_desc(s, s->wdma_sys_addr + offset, &desc)) {
        // FIXME: set error???
        g_assert_not_reached();
    }

    dma_memory_unmap(s->addr_space, data->data, s->wdma_len,
                     DMA_DIRECTION_FROM_DEVICE, data->size);

    if (desc_interrupt &&
        (qatomic_read(&s->wdma_settings) & SW_RWDMA_SETTINGS_DESC_INT)) {
        // FIXME: do we even need this mutex here?
        int locked = qemu_mutex_trylock(&s->wdma_mutex);
        qatomic_or(&s->wdma_status, SW_RWDMA_SETTINGS_DESC_INT);
        if (locked) {
            qemu_mutex_unlock(&s->wdma_mutex);
        }

        rcm_sw_update_irq(s);
    }

    // переход на следующий
    s->wdma_tbl_size -= sizeof(sw_dma_desc_t);
    if (!s->wdma_tbl_size) {
        s->wdma_tbl_size = s->wdma_tbl_size_internal;
    }

    // FIXME: do we even need this mutex here?
    int locked = qemu_mutex_trylock(&s->wdma_mutex);
    rcm_sw_wdma_recv(s);
    if (locked) {
        qemu_mutex_unlock(&s->wdma_mutex);
    }
}

static void rcm_sw_write_done(sw_data *data, void *user_data)
{
    RCMSpaceWireState *s = user_data;

    // взять текущий дескриптор
    sw_dma_desc_t desc = {0};
    uint32_t offset = s->rdma_tbl_size_internal - s->rdma_tbl_size;

    if (read_dma_desc(s, s->rdma_sys_addr + offset, &desc)) {
        // FIXME: set error???
        g_assert_not_reached();
    }

    bool desc_interrupt = desc.interrupt ? true : false;

    // обработать текущий дескриптор
    desc.activity = SW_DESC_ACTIVITY_COMPL;
    desc.length = data->size;
    desc.connection_error = 0; //FIXME: тут был раньше sw_status: status == SW_OK ? 0 : 1;

    if (write_dma_desc(s, s->rdma_sys_addr + offset, &desc)) {
        // FIXME: set error???
        g_assert_not_reached();
    }

    dma_memory_unmap(s->addr_space, data->data, s->rdma_len,
                     DMA_DIRECTION_FROM_DEVICE, data->size);

    if (desc_interrupt &&
        (qatomic_read(&s->rdma_settings) & SW_RWDMA_SETTINGS_DESC_INT)) {
        // FIXME: do we even need this mutex here?
        int locked = qemu_mutex_trylock(&s->rdma_mutex);
        qatomic_or(&s->rdma_status, SW_RWDMA_SETTINGS_DESC_INT);
        if (locked) {
            qemu_mutex_unlock(&s->rdma_mutex);
        }

        rcm_sw_update_irq(s);
    }

    // переход на следующий дескриптор
    s->rdma_tbl_size -= sizeof(sw_dma_desc_t);
    if (!s->rdma_tbl_size) {
        s->rdma_tbl_size = s->rdma_tbl_size_internal;
    }

    // FIXME: do we even need this mutex here?
    int locked = qemu_mutex_trylock(&s->rdma_mutex);
    rcm_sw_rdma_send(s);
    if (locked) {
            qemu_mutex_unlock(&s->rdma_mutex);
    }
}

static void rcm_sw_logic_send_to_chardev(void *buf, uint32_t length, void *user_data)
{
    RCMSpaceWireState *s = user_data;

    qemu_chr_fe_write_all(&s->chardev, buf, length);
}

static int rcm_sw_chardev_can_receive(void *opaque)
{
    (void)opaque;
    // FIXME: what to do with this number
    return 1024;
}

static void rcm_sw_chardev_receive(void *opaque, const uint8_t *data_char, int size)
{
    RCMSpaceWireState *s = opaque;

    sw_logic_data_from_interface(s->sw_logic, data_char, size);
}

static void rcm_sw_chardev_event(void* opaque, QEMUChrEvent evt) {
    RCMSpaceWireState *s = RCM_SPACEWIRE(opaque);

    switch (evt) {
    case CHR_EVENT_OPENED:
        sw_logic_interface_connected(s->sw_logic);
        break;

    case CHR_EVENT_CLOSED:
        sw_logic_interface_disconnected(s->sw_logic);
        break;

    default:
        break;
    }
}

/*
 * device code
 */
static void rcm_sw_realize(DeviceState *dev, Error **errp)
{
    RCMSpaceWireState *s = RCM_SPACEWIRE(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->iomem, OBJECT(dev), &rcm_sw_ops, s, "rcm_spacewire",
                          0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->core_irq);
    sysbus_init_irq(sbd, &s->dma_irq);

    qemu_mutex_init(&s->rdma_mutex);
    qemu_mutex_init(&s->wdma_mutex);

    s->sw_logic = sw_logic_new(rcm_sw_logic_send_to_chardev, rcm_sw_write_done,
                               rcm_sw_read_done, s);

    qemu_chr_fe_set_handlers(&s->chardev, rcm_sw_chardev_can_receive,
                             rcm_sw_chardev_receive, rcm_sw_chardev_event,
                             NULL, s, NULL, true);

    // set default address space
    if (s->addr_space == NULL) {
        s->addr_space = &address_space_memory;
    }
}

void rcm_sw_change_address_space(RCMSpaceWireState *s, AddressSpace *addr_space,
                                 Error **errp)
{
    if (object_property_get_bool(OBJECT(s), "realized", errp)) {
        error_setg(errp, "Can't change address_space of realized device\n");
    }

    s->addr_space = addr_space;
}

static Property rcm_sw_properties[] = {
    DEFINE_PROP_CHR("chardev", RCMSpaceWireState, chardev),
    DEFINE_PROP_END_OF_LIST(),
};

static void rcm_sw_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "RC Module spacewire controller";
    dc->realize = rcm_sw_realize;
    dc->reset = rcm_sw_reset;
    device_class_set_props(dc, rcm_sw_properties);
}

static const TypeInfo rcm_sw_info = {
    .name = TYPE_RCM_SPACEWIRE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RCMSpaceWireState),
    .class_init = rcm_sw_class_init,
};

static void rcm_sw_register_type(void)
{
    type_register_static(&rcm_sw_info);
}

type_init(rcm_sw_register_type)
