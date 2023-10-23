#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "hw/boards.h"
#include "cpu.h"
#include "sysemu/reset.h"
#include "sysemu/sysemu.h"
#include "hw/qdev-properties.h"
#include "hw/ppc/ppc.h"
#include "hw/ppc/dcr_mpic.h"
#include "hw/char/pl011.h"
#include "hw/irq.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"

typedef struct {
    MachineState parent;

    PowerPCCPU *cpu;

    MpicState mpic;

    PL011State uart[2];

    DeviceState *gpio[1];

    /* board properties */
    uint8_t boot_cfg;
}  MT174MachineState;

#define TYPE_MT174_MACHINE MACHINE_TYPE_NAME("mt174.04")
#define MT174_MACHINE(obj) \
    OBJECT_CHECK(MT174MachineState, obj, TYPE_MT174_MACHINE)

#define MT174_BOOT_CFG_DEFVAL 0x82

/* DCR registers */
static int dcr_read_error(int dcrn)
{
    printf("DCR: error reading register with address 0x%x\n", dcrn);
    return -1;
}

static int dcr_write_error(int dcrn)
{
    printf("DCR: error writing register with address 0x%x\n", dcrn);
    return -1;
}

static uint32_t plb4arb8m_dcr_read (void *opaque, int dcrn)
{
    return 0;
}

static void plb4arb8m_dcr_write (void *opaque, int dcrn, uint32_t val)
{
}

static void dcr_plb4arb8m_register(CPUPPCState *env, uint32_t base)
{
    ppc_dcr_register(env, base + 0x2, NULL, plb4arb8m_dcr_read, plb4arb8m_dcr_write);
    ppc_dcr_register(env, base + 0x3, NULL, plb4arb8m_dcr_read, plb4arb8m_dcr_write);
    ppc_dcr_register(env, base + 0x4, NULL, plb4arb8m_dcr_read, plb4arb8m_dcr_write);
    ppc_dcr_register(env, base + 0x6, NULL, plb4arb8m_dcr_read, plb4arb8m_dcr_write);
    ppc_dcr_register(env, base + 0x7, NULL, plb4arb8m_dcr_read, plb4arb8m_dcr_write);
}

static uint32_t dmaplb6_dcr_read (void *opaque, int dcrn)
{
    return 0;
}

static void dmaplb6_dcr_write (void *opaque, int dcrn, uint32_t val)
{
}

static void dcr_dmaplb6_register(CPUPPCState *env, uint32_t base)
{
    uint32_t i;

    for (i = 0; i <= 0x4b; i++) {
        ppc_dcr_register(env, base + i, NULL, dmaplb6_dcr_read, dmaplb6_dcr_write);
    }
}

static uint32_t p6bc_dcr_read (void *opaque, int dcrn)
{
    return 0;
}

static void p6bc_dcr_write (void *opaque, int dcrn, uint32_t val)
{
}

static void dcr_p6bc_register(CPUPPCState *env, uint32_t base)
{
    uint32_t i;

    for (i = 0; i <= 0x11; i++) {
        ppc_dcr_register(env, base + i, NULL, p6bc_dcr_read, p6bc_dcr_write);
    }
}

static uint32_t dcrarb_dcr_read (void *opaque, int dcrn)
{
    return 0;
}

static void dcrarb_dcr_write (void *opaque, int dcrn, uint32_t val)
{
}

static void dcr_dcrarb_register(CPUPPCState *env, uint32_t base)
{
    uint32_t i;

    for (i = 0; i <= 0x7; i++) {
        ppc_dcr_register(env, base + i, NULL, dcrarb_dcr_read, dcrarb_dcr_write);
    }
}

static uint32_t ddr_mclfir_dcr_read (void *opaque, int dcrn)
{
    return 0;
}

static void ddr_mclfir_dcr_write (void *opaque, int dcrn, uint32_t val)
{
}

static void dcr_ddr_mclfir_register(CPUPPCState *env, uint32_t base)
{
    uint32_t i;

    for (i = 0x0; i <= 0x35; i++) {
        ppc_dcr_register(env, base + i, NULL, ddr_mclfir_dcr_read, ddr_mclfir_dcr_write);
    }
}

static uint32_t ddr_plb6mcif2_dcr_read (void *opaque, int dcrn)
{
    return 0;
}

static void ddr_plb6mcif2_dcr_write (void *opaque, int dcrn, uint32_t val)
{
}

static void dcr_ddr_plb6mcif2_register(CPUPPCState *env, uint32_t base)
{
    uint32_t i;

    for (i = 0x0; i <= 0x3f; i++) {
        ppc_dcr_register(env, base + i, NULL, ddr_plb6mcif2_dcr_read, ddr_plb6mcif2_dcr_write);
    }
}

static uint32_t sctl_dcr_read (void *opaque, int dcrn)
{
    MT174MachineState *s = opaque;

    if (dcrn == 0x80090000) {
        return s->boot_cfg;
    }

    return 0;
}

static void sctl_dcr_write (void *opaque, int dcrn, uint32_t val)
{
}

static void dcr_sctl_register(CPUPPCState *env, uint32_t base, void *opaque)
{
    uint32_t i;

    for (i = 0x0; i <= 0x100; i++) {
        ppc_dcr_register(env, base + i, opaque, sctl_dcr_read, sctl_dcr_write);
    }
}

static uint32_t dcr_unknown_read (void *opaque, int dcrn)
{
    return 0;
}

static void dcr_unknown_write (void *opaque, int dcrn, uint32_t val)
{
}

static void dcr_unknown16(CPUPPCState *env, uint32_t base)
{
    uint32_t i;

    for (i = 0x0; i < 0x10; i++) {
        ppc_dcr_register(env, base + i, NULL, dcr_unknown_read, dcr_unknown_write);
    }
}

static void dcr_unknown256(CPUPPCState *env, uint32_t base)
{
    uint32_t i;

    for (i = 0x0; i < 0x100; i++) {
        ppc_dcr_register(env, base + i, NULL, dcr_unknown_read, dcr_unknown_write);
    }
}

static void dcr_unknown64k(CPUPPCState *env, uint32_t base)
{
    uint32_t i;

    for (i = 0x0; i < 0x10000; i++) {
        ppc_dcr_register(env, base + i, NULL, dcr_unknown_read, dcr_unknown_write);
    }
}

/* Machine init */
static void create_initial_mapping(CPUPPCState *env)
{
    ppcemb_tlb_t *tlb = &env->tlb.tlbe[0xf0 + 3 * env->tlb_per_way];

    tlb->attr = 0;
    tlb->prot = PAGE_VALID | ((PAGE_READ | PAGE_WRITE | PAGE_EXEC) << 4);
    tlb->size = 4*KiB;
    tlb->EPN = 0xfffff000 & TARGET_PAGE_MASK;
    tlb->RPN = 0x3fffffff000;
    tlb->PID = 0;
}

static void cpu_reset_temp(void *opaque)
{
    PowerPCCPU *cpu = opaque;

    cpu_reset(CPU(cpu));

    /* Create mapping */
    create_initial_mapping(&cpu->env);
}

static void mt174_init(MachineState *machine)
{
    MT174MachineState *s = MT174_MACHINE(machine);
    SysBusDevice *busdev;

    /* init CPUs */
    s->cpu = POWERPC_CPU(cpu_create(machine->cpu_type));
    ppc_booke_timers_init(s->cpu, 800000000, 0);

    CPUPPCState *env = &s->cpu->env;
    ppc_dcr_init(env, dcr_read_error, dcr_write_error);

    dcr_plb4arb8m_register(env, 0x00000010);
    dcr_unknown16(env, 0x30);
    dcr_unknown16(env, 0x40);
    dcr_unknown16(env, 0x50);
    dcr_plb4arb8m_register(env, 0x00000060);

    dcr_dmaplb6_register(env, 0x80000100);
    dcr_p6bc_register(env, 0x80000200);
    dcr_unknown256(env, 0x80000300);
    dcr_unknown256(env, 0x80000400);
    dcr_unknown256(env, 0x80000600);
    dcr_dcrarb_register(env, 0x80000700);
    dcr_unknown256(env, 0x80000800);
    dcr_unknown256(env, 0x80000900);
    dcr_unknown256(env, 0x80000A00);

    dcr_ddr_plb6mcif2_register(env, 0x80050000);
    dcr_unknown64k(env, 0x80060000);
    dcr_ddr_mclfir_register(env, 0x80070000);
    dcr_unknown64k(env, 0x80080000);
    dcr_sctl_register(env, 0x80090000, s);
    dcr_unknown64k(env, 0x800a0000);
    dcr_unknown64k(env, 0x800b0000);
    dcr_unknown64k(env, 0x800c0000);
    dcr_unknown64k(env, 0x800d0000);

    // FIXME: данный диапазон DCR регистров не приведен в документации
    // но используется в rumboot-е...
    dcr_unknown64k(env, 0x800e0000);

    object_initialize_child(OBJECT(s), "mpic", &s->mpic, TYPE_MPIC);
    object_property_set_int(OBJECT(&s->mpic), "baseaddr", 0xffc00000, &error_fatal);
    object_property_set_link(OBJECT(&s->mpic), "cpu-state", OBJECT(s->cpu), &error_fatal);
    qdev_realize(DEVICE(&s->mpic), NULL, &error_fatal);
    qdev_connect_gpio_out_named(DEVICE(&s->mpic), "non_crit_int", 0,
                                qdev_get_gpio_in(DEVICE(s->cpu), PPC40x_INPUT_INT));
    qdev_connect_gpio_out_named(DEVICE(&s->mpic), "crit_int", 0,
                                qdev_get_gpio_in(DEVICE(s->cpu), PPC40x_INPUT_CINT));

    MemoryRegion *EMI = g_new(MemoryRegion, 1);
    memory_region_init_ram(EMI, NULL, "EMI", 0x200000000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x0, EMI);

    MemoryRegion *IM0 = g_new(MemoryRegion, 1);
    memory_region_init_ram(IM0, NULL, "IM0", 0x20000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x1080000000, IM0);

    MemoryRegion *rom = g_new(MemoryRegion, 1);
    memory_region_init_rom(rom, NULL, "rom", 0x10000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x1fffff0000, rom);

    MemoryRegion *IM1 = g_new(MemoryRegion, 1);
    memory_region_init_ram(IM1, NULL, "IM1", 0x20000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x20c0000000, IM1);

    MemoryRegion *mko0 = g_new(MemoryRegion, 1);
    memory_region_init_ram(mko0, NULL, "mko0", 0x1000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x20c0020000, mko0);

    MemoryRegion *mko2 = g_new(MemoryRegion, 1);
    memory_region_init_ram(mko2, NULL, "mko2", 0x1000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x20c0021000, mko2);

    s->gpio[0] = sysbus_create_simple("pl061", 0x20c0028000, NULL);

    if (serial_hd(0)) {
        object_initialize_child(OBJECT(s), "uart0", &s->uart[0], TYPE_PL011);
        qdev_prop_set_chr(DEVICE(&s->uart[0]), "chardev", serial_hd(0));
        sysbus_realize(SYS_BUS_DEVICE(&s->uart[0]), &error_fatal);
        busdev = SYS_BUS_DEVICE(&s->uart[0]);
        memory_region_add_subregion(get_system_memory(), 0x20c0029000,
                                    sysbus_mmio_get_region(busdev, 0));
        // sysbus_connect_irq(busdev, 0, qdev_get_gpio_in(DEVICE(&s->mpic), 101));
    }

    MemoryRegion *eth0 = g_new(MemoryRegion, 1);
    memory_region_init_ram(eth0, NULL, "eth0", 0x1000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x20c002a000, eth0);

    MemoryRegion *spi0 = g_new(MemoryRegion, 1);
    memory_region_init_ram(spi0, NULL, "spi0", 0x1000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x20c002b000, spi0);

    MemoryRegion *sdio0 = g_new(MemoryRegion, 1);
    memory_region_init_ram(sdio0, NULL, "sdio0", 0x1000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x20c002c000, sdio0);

    MemoryRegion *mko1 = g_new(MemoryRegion, 1);
    memory_region_init_ram(mko1, NULL, "mko1", 0x1000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x20c0030000, mko1);

    MemoryRegion *mko3 = g_new(MemoryRegion, 1);
    memory_region_init_ram(mko3, NULL, "mko3", 0x1000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x20c0031000, mko3);

    MemoryRegion *gpio1 = g_new(MemoryRegion, 1);
    memory_region_init_ram(gpio1, NULL, "gpio1", 0x1000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x20c0038000, gpio1);

    if (serial_hd(1)) {
        object_initialize_child(OBJECT(s), "uart1", &s->uart[0], TYPE_PL011);
        qdev_prop_set_chr(DEVICE(&s->uart[0]), "chardev", serial_hd(0));
        sysbus_realize(SYS_BUS_DEVICE(&s->uart[0]), &error_fatal);
        busdev = SYS_BUS_DEVICE(&s->uart[0]);
        memory_region_add_subregion(get_system_memory(), 0x20c0039000,
                                    sysbus_mmio_get_region(busdev, 0));
        // sysbus_connect_irq(busdev, 0, qdev_get_gpio_in(DEVICE(&s->mpic), 101));
    }

    MemoryRegion *eth1 = g_new(MemoryRegion, 1);
    memory_region_init_ram(eth1, NULL, "eth1", 0x1000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x20c003a000, eth1);

    MemoryRegion *spi1 = g_new(MemoryRegion, 1);
    memory_region_init_ram(spi1, NULL, "spi1", 0x1000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x20c003b000, spi1);

    MemoryRegion *sdio1 = g_new(MemoryRegion, 1);
    memory_region_init_ram(sdio1, NULL, "sdio1", 0x1000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x20c003c000, sdio1);

    MemoryRegion *IM2 = g_new(MemoryRegion, 1);
    memory_region_init_ram(IM2, NULL, "IM2", 0x20000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x20c0040000, IM2);

    MemoryRegion *IM3 = g_new(MemoryRegion, 1);
    memory_region_init_ram(IM3, NULL, "IM3", 0x20000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x20c0060000, IM3);

    MemoryRegion *switch_axi32l = g_new(MemoryRegion, 1);
    memory_region_init_ram(switch_axi32l, NULL, "switch_axi32l", 0x100000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x20c0100000, switch_axi32l);

    MemoryRegion *switch_axi32r = g_new(MemoryRegion, 1);
    memory_region_init_ram(switch_axi32r, NULL, "switch_axi32r", 0x100000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x20c0200000, switch_axi32r);

    MemoryRegion *spacewire0 = g_new(MemoryRegion, 1);
    memory_region_init_ram(spacewire0, NULL, "spacewire0", 0x1000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x20c0300000, spacewire0);

    MemoryRegion *spacewire1 = g_new(MemoryRegion, 1);
    memory_region_init_ram(spacewire1, NULL, "spacewire1", 0x1000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x20c0301000, spacewire1);

    MemoryRegion *spacewire2 = g_new(MemoryRegion, 1);
    memory_region_init_ram(spacewire2, NULL, "spacewire2", 0x1000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x20c0302000, spacewire2);

    MemoryRegion *spacewire3 = g_new(MemoryRegion, 1);
    memory_region_init_ram(spacewire3, NULL, "spacewire3", 0x1000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x20c0303000, spacewire3);

    MemoryRegion *COM0 = g_new(MemoryRegion, 1);
    memory_region_init_ram(COM0, NULL, "COM0", 0x1000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x20c0304000, COM0);

    MemoryRegion *COM1 = g_new(MemoryRegion, 1);
    memory_region_init_ram(COM1, NULL, "COM1", 0x1000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x20c0305000, COM1);

    MemoryRegion *AXI_DMA = g_new(MemoryRegion, 1);
    memory_region_init_ram(AXI_DMA, NULL, "AXI_DMA", 0x1000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x20c0306000, AXI_DMA);

    MemoryRegion *SCRB = g_new(MemoryRegion, 1);
    memory_region_init_ram(SCRB, NULL, "SCRB", 0x1000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x20c0307000, SCRB);

    MemoryRegion *switch_axi64 = g_new(MemoryRegion, 1);
    memory_region_init_ram(switch_axi64, NULL, "switch_axi64", 0x100000, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x20c0400000, switch_axi64);

    MemoryRegion *rom_alias = g_new(MemoryRegion, 1);
    memory_region_init_alias(rom_alias, NULL, "rom_alias", rom, 0, 0x10000);
    memory_region_add_subregion(get_system_memory(), 0x3ffffff0000, rom_alias);


    qemu_register_reset(cpu_reset_temp, s->cpu);
}

static void mt174_reset(MachineState *machine, ShutdownCause reason)
{
    MT174MachineState *s = MT174_MACHINE(machine);

    // default action
    qemu_devices_reset(reason);

    // FIXME: не надо ли как-то по-другому помещать прошивку в память?
    {
        uint32_t file_size = 64*KiB;
        uint8_t data[64*KiB];
        int fd = open(machine->firmware, O_RDONLY);

        if (fd == -1) {
            printf("No bios file '%s' found\n", machine->firmware);
            exit(-1);
        }

        if (read(fd, data, file_size) != file_size) {
            printf("File size is less then expected %u bytes\n", file_size);
        }

        close(fd);

        address_space_write_rom(&address_space_memory, 0x3ffffff0000,
            MEMTXATTRS_UNSPECIFIED, data, file_size);
    }

    // Set GPIO0 pins
    uint32_t boot_cfg = s->boot_cfg;
    for (int i = 0; boot_cfg; i++, boot_cfg >>= 1) {
        if (boot_cfg & (1<<i)) {
            qemu_irq_raise(qdev_get_gpio_in(s->gpio[0], i));
        }
    }
}

static void mt174_boot_cfg_get_and_set(Object *obj, Visitor *v, const char *name,
                                        void *opaque, Error **errp)
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
    mc->default_cpu_type = POWERPC_CPU_TYPE_NAME("476fp");

    ObjectProperty *prop;
    prop = object_class_property_add(oc, "boot-cfg", "uint8", mt174_boot_cfg_get_and_set,
                                     mt174_boot_cfg_get_and_set, NULL, NULL);
    object_property_set_default_uint(prop, MT174_BOOT_CFG_DEFVAL);
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
