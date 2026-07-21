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

#define TYPE_MK19404_MACHINE MACHINE_TYPE_NAME("mk194.04")
#define MK19404_MACHINE(obj) \
    OBJECT_CHECK(MK19404MachineState, obj, TYPE_MK19404_MACHINE)

typedef struct {
    /*< private >*/
    MachineState parent;

    /*< public >*/
    O32TState *soc;

    /* boot properties */
    uint8_t boot_cfg;
    char *mram_path;
} MK19404MachineState;

static void mk19404_init(MachineState *machine)
{
    MK19404MachineState *s = MK19404_MACHINE(machine);

    s->soc = (O32TState *)qdev_new(TYPE_O32T);
    object_property_add_child(OBJECT(s), "O32T", OBJECT(s->soc));
    qdev_prop_set_uint8(DEVICE(s->soc), "boot-cfg", s->boot_cfg);
    qdev_prop_set_string(DEVICE(s->soc), "firmware", machine->firmware);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(s->soc), &error_fatal);

    /* NOR at bank 0 */
    DriveInfo *dinfo = drive_get(IF_PFLASH, 0, 0);
    if (dinfo) {
        DeviceState *pflash = qdev_new("cfi.pflash02");

        qdev_prop_set_drive(pflash, "drive", blk_by_legacy_dinfo(dinfo));

        // TODO: we can get input file size using blk_getlength().
        // do we need it?
        qdev_prop_set_uint32(pflash, "num-blocks", 128);
        qdev_prop_set_uint32(pflash, "sector-length", 128 * KiB);

        qdev_prop_set_uint8(pflash, "width", 4);
        // replicate nor-flash to fill the bank of 256 MB
        // qdev_prop_set_uint8(pflash, "mappings", 4);
        qdev_prop_set_uint8(pflash, "big-endian", 0);
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
            oi10_o32t_get_ext_mem_region(DEVICE(s->soc)), 0x0,
            pflash_region, 1);
    }

    /* MRAM at bank 5 */
    g_assert(s->mram_path != NULL);
    MemoryRegion *mram = g_new(MemoryRegion, 1);
    memory_region_init_ram_from_file(mram, NULL, "mram", 512 * KiB, 0, RAM_SHARED,
                                     s->mram_path, 0, &error_fatal);
    memory_region_add_subregion_overlap(oi10_o32t_get_ext_mem_region(DEVICE(s->soc)),
                                        0x70000000, mram, 1);

    MemoryRegion *mram_alias = g_new(MemoryRegion, 1);
    memory_region_init_alias(mram_alias, NULL, "mram_a", mram, 0, 512 * KiB);
    memory_region_add_subregion_overlap(oi10_o32t_get_ext_mem_region(DEVICE(s->soc)),
                                        0x80000000 - 512 * KiB, mram_alias, 1);
}

static void mk19404_reset(MachineState *machine, ShutdownCause reason)
{
    // default action
    qemu_devices_reset(reason);
}

static void mk19404_boot_cfg_get_and_set(Object *obj, Visitor *v,
                                       const char *name, void *opaque,
                                       Error **errp)
{
    MK19404MachineState *s = MK19404_MACHINE(obj);

    visit_type_uint8(v, name, &s->boot_cfg, errp);
}

static void mk19404_mram_path_set(Object *obj, Visitor *v, const char *name,
                                  void *opaque, Error **errp)
{
    MK19404MachineState *s = MK19404_MACHINE(obj);
    visit_type_str(v, name, &s->mram_path, errp);
}

static void mk19404_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "MK194.04 board";

    mc->init = mk19404_init;
    mc->reset = mk19404_reset;

    ObjectProperty *prop = object_class_property_add(
        oc, "boot-cfg", "uint8", mk19404_boot_cfg_get_and_set,
        mk19404_boot_cfg_get_and_set, NULL, NULL);
    object_property_set_default_uint(prop, OI10_O32T_BOOT_CFG_DEFVAL);
    object_class_property_add(oc, "mram", "string", NULL, mk19404_mram_path_set, NULL,
                              NULL);
}

static const TypeInfo mk19404_info = {
    .name = TYPE_MK19404_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(MK19404MachineState),
    .class_init = mk19404_class_init,
};

static void mk19404_machines_init(void)
{
    type_register_static(&mk19404_info);
}

type_init(mk19404_machines_init)
