#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "exec/address-spaces.h"
#include "sysemu/reset.h"
#include "hw/boards.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "hw/sd/sd.h"
#include "hw/ppc/rcm_oi10_o32t.h"

#define TYPE_MT174_MACHINE MACHINE_TYPE_NAME("mt174.04")
#define MT174_MACHINE(obj) \
    OBJECT_CHECK(MT174MachineState, obj, TYPE_MT174_MACHINE)

typedef struct {
    /*< private >*/
    MachineState parent;

    /*< public >*/
    O32TState *soc;

    /* boot properties */
    uint8_t boot_cfg;
} MT174MachineState;

static void mt174_init(MachineState *machine)
{
    MT174MachineState *s = MT174_MACHINE(machine);

    s->soc = (O32TState *)qdev_new(TYPE_O32T);
    object_property_add_child(OBJECT(s), "O32T", OBJECT(s->soc));
    qdev_prop_set_uint8(DEVICE(s->soc), "boot-cfg", s->boot_cfg);
    qdev_prop_set_string(DEVICE(s->soc), "firmware", machine->firmware);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(s->soc), &error_fatal);

    DriveInfo *dinfo = drive_get(IF_PFLASH, 0, 0);
    if (dinfo) {
        DeviceState *pflash = qdev_new("cfi.pflash02");

        qdev_prop_set_drive(pflash, "drive", blk_by_legacy_dinfo(dinfo));

        // TODO: we can get input file size using blk_getlength().
        // do we need it?
        qdev_prop_set_uint32(pflash, "num-blocks", 256);
        qdev_prop_set_uint32(pflash, "sector-length", 256 * KiB);

        qdev_prop_set_uint8(pflash, "width", 4);
        // replicate nor-flash to fill the bank of 256 MB
        qdev_prop_set_uint8(pflash, "mappings", 4);
        qdev_prop_set_uint8(pflash, "big-endian", 1);
        qdev_prop_set_uint16(pflash, "id0", 0x0001);
        qdev_prop_set_uint16(pflash, "id1", 0x0000);
        qdev_prop_set_uint16(pflash, "id2", 0x0003);
        qdev_prop_set_uint16(pflash, "id3", 0x0001);
        qdev_prop_set_uint16(pflash, "unlock-addr0", 0x0555);
        qdev_prop_set_uint16(pflash, "unlock-addr1", 0x02AA);
        qdev_prop_set_string(pflash, "name", "nor_flash");
        sysbus_realize_and_unref(SYS_BUS_DEVICE(pflash), &error_fatal);

        MemoryRegion *pflash_region =
            sysbus_mmio_get_region(SYS_BUS_DEVICE(pflash), 0);
        memory_region_add_subregion_overlap(
            oi10_o32t_get_ext_mem_region(DEVICE(s->soc)), 0x70000000,
            pflash_region, 1);
    }

    dinfo = drive_get(IF_SD, 0, 0);
    if (dinfo) {
        DeviceState *card;

        card = qdev_new(TYPE_SD_CARD);
        qdev_prop_set_drive_err(card, "drive", blk_by_legacy_dinfo(dinfo),
                                &error_fatal);
        qdev_prop_set_uint8(card, "spec_version", SD_PHY_SPECv3_01_VERS);
        qdev_realize_and_unref(
            card, oi10_o32t_get_sdio_bus(DEVICE(s->soc), 0),
            &error_fatal);
    }
}

static void mt174_reset(MachineState *machine, ShutdownCause reason)
{
    // default action
    qemu_devices_reset(reason);
}

static void mt174_boot_cfg_get_and_set(Object *obj, Visitor *v,
                                       const char *name, void *opaque,
                                       Error **errp)
{
    MT174MachineState *s = MT174_MACHINE(obj);

    visit_type_uint8(v, name, &s->boot_cfg, errp);
}

static void mt174_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "MT174.04 board";

    mc->init = mt174_init;
    mc->reset = mt174_reset;

    ObjectProperty *prop = object_class_property_add(
        oc, "boot-cfg", "uint8", mt174_boot_cfg_get_and_set,
        mt174_boot_cfg_get_and_set, NULL, NULL);
    object_property_set_default_uint(prop, OI10_O32T_BOOT_CFG_DEFVAL);
}

static const TypeInfo mt174_info = {
    .name = TYPE_MT174_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(MT174MachineState),
    .class_init = mt174_class_init,
};

static void mt174_machines_init(void)
{
    type_register_static(&mt174_info);
}

type_init(mt174_machines_init)
