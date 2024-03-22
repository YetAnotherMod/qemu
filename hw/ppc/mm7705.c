#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "hw/boards.h"
#include "cpu.h"
#include "sysemu/reset.h"
#include "sysemu/sysemu.h"
#include "sysemu/block-backend.h"
#include "hw/qdev-properties.h"
#include "hw/ppc/ppc.h"
#include "hw/ppc/dcr_mpic.h"
#include "hw/char/pl011.h"
#include "hw/ssi/pl022.h"
#include "hw/ssi/ssi.h"
#include "hw/net/greth.h"
#include "hw/sd/sd.h"
#include "hw/sd/keyasic_sd.h"
#include "hw/irq.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "hw/misc/unimp.h"

typedef struct {
    MachineState parent;

    PowerPCCPU *cpu;

    MpicState mpic;

    PL011State uart[3];

    struct PL022State spi[3];

    DeviceState *lsif0_mgpio[11];

    DeviceState *lsif1_gpio[2];
    DeviceState *lsif1_mgpio[5];

    GRETHState greth[3];
    GRETHState gb_greth[2];

    KeyasicSdState sdio;

    /* board properties */
    uint8_t boot_cfg;
} MM7705MachineState;

#define TYPE_MM7705_MACHINE MACHINE_TYPE_NAME("mb115.01")
#define MM7705_MACHINE(obj) \
    OBJECT_CHECK(MM7705MachineState, obj, TYPE_MM7705_MACHINE)

#define MM7705_BOOT_CFG_DEFVAL 0x16

#define MM7705_DDR_SLOTS_CNT 2

/* DCR registers */
static int dcr_read_error(int dcrn)
{
    printf("DCR: error reading register with address 0x%x\n", dcrn);
    return 0;
}

static int dcr_write_error(int dcrn)
{
    printf("DCR: error writing register with address 0x%x\n", dcrn);
    return 0;
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

static uint32_t itrace_dcr_read (void *opaque, int dcrn)
{
    return 0;
}

static void itrace_dcr_write (void *opaque, int dcrn, uint32_t val)
{
}

static void dcr_itrace_register(CPUPPCState *env, uint32_t base)
{
    uint32_t i;

    for (i = 0; i <= 0xb; i++) {
        ppc_dcr_register(env, base + i, NULL, itrace_dcr_read, itrace_dcr_write);
    }
}

static uint32_t ltrace_dcr_read (void *opaque, int dcrn)
{
    return 0;
}

static void ltrace_dcr_write (void *opaque, int dcrn, uint32_t val)
{
}

static void dcr_ltrace_register(CPUPPCState *env, uint32_t base)
{
    uint32_t i;

    for (i = 0; i <= 0x15; i++) {
        ppc_dcr_register(env, base + i, NULL, ltrace_dcr_read, ltrace_dcr_write);
    }
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

static uint32_t ddr_graif_dcr_read (void *opaque, int dcrn)
{
    return 0;
}

static void ddr_graif_dcr_write (void *opaque, int dcrn, uint32_t val)
{
}

static void dcr_ddr_graif_register(CPUPPCState *env, uint32_t base)
{
    uint32_t i;

    for (i = 0x0; i <= 0xfb; i++) {
        ppc_dcr_register(env, base + i, NULL, ddr_graif_dcr_read, ddr_graif_dcr_write);
    }
}

/* Dummy DDR34LMC. Needed in DDR initialisation procedure */
struct ddr3lmc {
    uint32_t mcstat;
};

static uint32_t ddr_ddr3lmc_dcr_read (void *opaque, int dcrn)
{
    if ((dcrn & (0x1000 - 1)) == 0x10) {
        struct ddr3lmc *ddr3lmc = opaque;
        return ddr3lmc->mcstat;
    }

    return 0;
}

static void ddr_ddr3lmc_dcr_write (void *opaque, int dcrn, uint32_t val)
{
    if ((dcrn & (0x1000 - 1)) == 0x21) {
        struct ddr3lmc *ddr3lmc = opaque;

        if (val & 0x1000) {
            ddr3lmc->mcstat |= 0x10000000;
        }

        if (val & 0x20000000) {
            ddr3lmc->mcstat |= 0x80000000;
        }
    }
}

static void dcr_ddr_ddr3lmc_register(CPUPPCState *env, uint32_t base)
{
    uint32_t i;

    struct ddr3lmc *ddr3lmc = calloc(1, sizeof(struct ddr3lmc));
    assert(ddr3lmc != NULL);

    ddr3lmc->mcstat = 0x60000000;

    for (i = 0x0; i <= 0xfb; i++) {
        ppc_dcr_register(env, base + i, ddr3lmc,
                         ddr_ddr3lmc_dcr_read, ddr_ddr3lmc_dcr_write);
    }
}

static uint32_t ddr_aximcif2_dcr_read (void *opaque, int dcrn)
{
    return 0;
}

static void ddr_aximcif2_dcr_write (void *opaque, int dcrn, uint32_t val)
{
}

static void dcr_ddr_aximcif2_register(CPUPPCState *env, uint32_t base)
{
    uint32_t i;

    for (i = 0x0; i <= 0x20; i++) {
        ppc_dcr_register(env, base + i, NULL, ddr_aximcif2_dcr_read, ddr_aximcif2_dcr_write);
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

    for (i = 0x0; i <= 0x10; i++) {
        ppc_dcr_register(env, base + i, NULL, dcr_unknown_read, dcr_unknown_write);
    }
}

static uint64_t cpu_pll_read(void *opaque, hwaddr offset, unsigned size)
{
    return 1;
}

static const MemoryRegionOps cpu_pll_ops = {
    .read = cpu_pll_read,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

/* Dummy DDR PHY. Needed in DDR initialisation procedure */
struct ddr_phy {
    uint32_t PHYREG02;
    uint32_t PHYREGF0;
    uint32_t PHYREGFF;
};

static uint64_t ddr_phy_read(void *opaque, hwaddr offset, unsigned size)
{
    struct ddr_phy *ddr_phy = opaque;

    switch (offset) {
    case 0x8:
        return ddr_phy->PHYREG02;

    case 0x3c0:
        return ddr_phy->PHYREGF0;

    case 0x3fc:
        return ddr_phy->PHYREGFF;

    default:
        return 0;
    }
}

static void ddr_phy_write(void *opaque, hwaddr offset, uint64_t data, unsigned size)
{
    struct ddr_phy *ddr_phy = opaque;

    switch (offset) {
    case 0x8:
        ddr_phy->PHYREG02 = data;

        if (ddr_phy->PHYREG02 & 0x4) {
            ddr_phy->PHYREGF0 = 0x1f;
        }

        if (ddr_phy->PHYREG02 & 0x1) {
            ddr_phy->PHYREGFF = 0x1f;
        }
        break;

    default:
        break;
    }
}

static const MemoryRegionOps ddr_phy_ops = {
    .read = ddr_phy_read,
    .write = ddr_phy_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

/* Machine init */
static void create_initial_mapping(CPUPPCState *env)
{
    ppcemb_tlb_t *tlb = &env->tlb.tlbe[0xf0 + 3 * env->tlb_per_way];

    tlb->attr = 0;
    tlb->prot = PAGE_VALID | ((PAGE_READ | PAGE_EXEC) << 4);
    tlb->size = 4 * KiB;
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

static void mm7705_init(MachineState *machine)
{
    MM7705MachineState *s = MM7705_MACHINE(machine);
    SysBusDevice *busdev;

    const uint32_t cpu_freq = 800 * 1000 * 1000;

    /* init CPUs */
    s->cpu = POWERPC_CPU(cpu_create(machine->cpu_type));
    ppc_booke_timers_init(s->cpu, cpu_freq, 0);

    CPUPPCState *env = &s->cpu->env;
    ppc_dcr_init(env, dcr_read_error, dcr_write_error);

    dcr_plb4arb8m_register(env, 0x00000010);
    dcr_plb4arb8m_register(env, 0x00000020);
    dcr_unknown16(env, 0x30);
    dcr_unknown16(env, 0x40);
    dcr_unknown16(env, 0x50);
    dcr_plb4arb8m_register(env, 0x00000060);
    dcr_plb4arb8m_register(env, 0x00000070);
    dcr_plb4arb8m_register(env, 0x00000080);
    dcr_plb4arb8m_register(env, 0x00000090);
    dcr_plb4arb8m_register(env, 0x000000a0);
    dcr_unknown16(env, 0xb0);
    dcr_unknown16(env, 0xc0);
    dcr_itrace_register(env, 0x80000900);
    dcr_itrace_register(env, 0x80000a00);
    dcr_ltrace_register(env, 0x80000b00);
    dcr_ltrace_register(env, 0x80000c00);
    dcr_dmaplb6_register(env, 0x80000100);
    dcr_dmaplb6_register(env, 0x80000d00);
    dcr_p6bc_register(env, 0x80000200);
    dcr_dcrarb_register(env, 0x80000800);

    dcr_ddr_plb6mcif2_register(env, 0x80010000);
    dcr_ddr_aximcif2_register(env, 0x80020000);
    dcr_ddr_mclfir_register(env, 0x80030000);
    dcr_ddr_graif_register(env, 0x80040000);
    dcr_ddr_ddr3lmc_register(env, 0x80050000);

    dcr_ddr_plb6mcif2_register(env, 0x80100000);
    dcr_ddr_aximcif2_register(env, 0x80110000);
    dcr_ddr_mclfir_register(env, 0x80120000);
    dcr_ddr_graif_register(env, 0x80130000);
    dcr_ddr_ddr3lmc_register(env, 0x80140000);

    dcr_ddr_plb6mcif2_register(env, 0x80160000);
    dcr_ddr_plb6mcif2_register(env, 0x80180000);

    object_initialize_child(OBJECT(s), "mpic", &s->mpic, TYPE_MPIC);
    object_property_set_int(OBJECT(&s->mpic), "baseaddr", 0xffc00000, &error_fatal);
    object_property_set_int(OBJECT(&s->mpic), "timer-freq", cpu_freq / 8, &error_fatal);
    object_property_set_link(OBJECT(&s->mpic), "cpu-state", OBJECT(s->cpu), &error_fatal);
    qdev_realize(DEVICE(&s->mpic), NULL, &error_fatal);
    qdev_connect_gpio_out_named(DEVICE(&s->mpic), "non_crit_int", 0,
                                qdev_get_gpio_in(DEVICE(s->cpu), PPC40x_INPUT_INT));
    qdev_connect_gpio_out_named(DEVICE(&s->mpic), "crit_int", 0,
                                qdev_get_gpio_in(DEVICE(s->cpu), PPC40x_INPUT_CINT));


    /* Board has separated AXI bus for all peripherial devices */
    MemoryRegion *axi_mem = g_new(MemoryRegion, 1);
    AddressSpace *axi_addr_space = g_new(AddressSpace, 1);
    memory_region_init(axi_mem, NULL, "axi_mem", ~0u);
    address_space_init(axi_addr_space, axi_mem, "axi_addr_space");


    MemoryRegion *EM0 = g_new(MemoryRegion, 1);
    memory_region_init_ram(EM0, NULL, "EM0", machine->ram_size / MM7705_DDR_SLOTS_CNT,
                           &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x0, EM0);

    MemoryRegion *EM0_alias = g_new(MemoryRegion, 1);
    memory_region_init_alias(EM0_alias, NULL, "EM0_alias", EM0, 0, 1 * GiB);
    memory_region_add_subregion(axi_mem, 0x40000000, EM0_alias);

    MemoryRegion *EM1 = g_new(MemoryRegion, 1);
    memory_region_init_ram(EM1, NULL, "EM1", machine->ram_size / MM7705_DDR_SLOTS_CNT,
                           &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x200000000, EM1);

    MemoryRegion *EM1_alias = g_new(MemoryRegion, 1);
    memory_region_init_alias(EM1_alias, NULL, "EM1_alias", EM1, 0, 2 * GiB);
    memory_region_add_subregion(axi_mem, 0x80000000, EM1_alias);

    MemoryRegion *EM2 = g_new(MemoryRegion, 1);
    memory_region_init_alias(EM2, NULL, "EM2", EM0, 0, 2 * GiB);
    memory_region_add_subregion(get_system_memory(), 0x400000000, EM2);

    MemoryRegion *EM3 = g_new(MemoryRegion, 1);
    memory_region_init_alias(EM3, NULL, "EM3", EM1, 0, 2 * GiB);
    memory_region_add_subregion(get_system_memory(), 0x600000000, EM3);


    MemoryRegion *IM0 = g_new(MemoryRegion, 1);
    memory_region_init_ram(IM0, NULL, "IM0", 256 * KiB, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x1000040000, IM0);

    MemoryRegion *IM0_alias = g_new(MemoryRegion, 1);
    memory_region_init_alias(IM0_alias, NULL, "IM0_alias", IM0, 0, 256 * KiB);
    memory_region_add_subregion(axi_mem, 0x40000, IM0_alias);


    MemoryRegion *IFSYS0 = g_new(MemoryRegion, 1);
    memory_region_init_ram(IFSYS0, NULL, "IFSYS0", 640 * MiB, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x1010000000, IFSYS0);

    MemoryRegion *APB0 = g_new(MemoryRegion, 1);
    memory_region_init_ram(APB0, NULL, "APB0", 64 * KiB, &error_fatal);
    memory_region_add_subregion_overlap(get_system_memory(), 0x1038000000,
                                APB0, -10);

    MemoryRegion *APB1 = g_new(MemoryRegion, 1);
    memory_region_init_ram(APB1, NULL, "APB1", 80 * KiB, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x1038010000, APB1);


    MemoryRegion *NIC301_A_CFG = g_new(MemoryRegion, 1);
    memory_region_init_ram(NIC301_A_CFG, NULL, "NIC301_A_CFG", 1 * MiB, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x1038100000, NIC301_A_CFG);

    MemoryRegion *NIC301_DSP0_CFG = g_new(MemoryRegion, 1);
    memory_region_init_ram(NIC301_DSP0_CFG, NULL, "NIC301_DSP0_CFG", 1 * MiB,
                           &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x1038200000, NIC301_DSP0_CFG);

    MemoryRegion *NIC301_DSP1_CFG = g_new(MemoryRegion, 1);
    memory_region_init_ram(NIC301_DSP1_CFG, NULL, "NIC301_DSP1_CFG", 1 * MiB,
                           &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x1038300000, NIC301_DSP1_CFG);


    MemoryRegion *DSP0_NM0 = g_new(MemoryRegion, 1);
    memory_region_init_ram(DSP0_NM0, NULL, "DSP0_NM0", 128 * KiB, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x1039000000, DSP0_NM0);

    MemoryRegion *DSP0_NM1 = g_new(MemoryRegion, 1);
    memory_region_init_ram(DSP0_NM1, NULL, "DSP0_NM1", 128 * KiB, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x1039020000, DSP0_NM1);

    MemoryRegion *DSP1_NM0 = g_new(MemoryRegion, 1);
    memory_region_init_ram(DSP1_NM0, NULL, "DSP1_NM0", 128 * KiB, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x1039040000, DSP1_NM0);

    MemoryRegion *DSP1_NM1 = g_new(MemoryRegion, 1);
    memory_region_init_ram(DSP1_NM1, NULL, "DSP1_NM1", 128 * KiB, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x1039060000, DSP1_NM1);

    MemoryRegion *I2S = g_new(MemoryRegion, 1);
    memory_region_init_ram(I2S, NULL, "I2S", 4 * KiB, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x1039080000, I2S);

    MemoryRegion *SPDIF = g_new(MemoryRegion, 1);
    memory_region_init_ram(SPDIF, NULL, "SPDIF", 4 * KiB, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x1039081000, SPDIF);


    MemoryRegion *IFSYS1 = g_new(MemoryRegion, 1);
    memory_region_init_ram(IFSYS1, NULL, "IFSYS1", 64 * MiB, &error_fatal);
    // make this memory as fallback
    memory_region_add_subregion_overlap(get_system_memory(), 0x103c000000,
                                IFSYS1, -10);

    if (serial_hd(0)) {
        object_initialize_child(OBJECT(s), "uart0", &s->uart[0], TYPE_PL011);
        qdev_prop_set_chr(DEVICE(&s->uart[0]), "chardev", serial_hd(0));
        sysbus_realize(SYS_BUS_DEVICE(&s->uart[0]), &error_fatal);
        busdev = SYS_BUS_DEVICE(&s->uart[0]);
        memory_region_add_subregion(get_system_memory(), 0x103c05d000,
                                    sysbus_mmio_get_region(busdev, 0));
        sysbus_connect_irq(busdev, 0, qdev_get_gpio_in(DEVICE(&s->mpic), 101));
    }

    if (serial_hd(1)) {
        object_initialize_child(OBJECT(s), "uart1", &s->uart[1], TYPE_PL011);
        qdev_prop_set_chr(DEVICE(&s->uart[1]), "chardev", serial_hd(1));
        sysbus_realize(SYS_BUS_DEVICE(&s->uart[1]), &error_fatal);
        busdev = SYS_BUS_DEVICE(&s->uart[1]);
        memory_region_add_subregion(get_system_memory(), 0x103c05e000,
                                    sysbus_mmio_get_region(busdev, 0));
        sysbus_connect_irq(busdev, 0, qdev_get_gpio_in(DEVICE(&s->mpic), 102));
    }

    if (serial_hd(2)) {
        object_initialize_child(OBJECT(s), "uart2", &s->uart[2], TYPE_PL011);
        qdev_prop_set_chr(DEVICE(&s->uart[2]), "chardev", serial_hd(2));
        sysbus_realize(SYS_BUS_DEVICE(&s->uart[2]), &error_fatal);
        busdev = SYS_BUS_DEVICE(&s->uart[2]);
        memory_region_add_subregion(get_system_memory(), 0x103c05f000,
                                    sysbus_mmio_get_region(busdev, 0));
        sysbus_connect_irq(busdev, 0, qdev_get_gpio_in(DEVICE(&s->mpic), 65));
    }

    {
        uint8_t edcl_mac[5][6] = {
            {0xec, 0x17, 0x66, 0x00, 0x00, 0x02},
            {0xec, 0x17, 0x66, 0x00, 0x00, 0x03},
            {0xec, 0x17, 0x66, 0x00, 0x00, 0x00},
            {0xec, 0x17, 0x66, 0x77, 0x05, 0x01},
            {0xec, 0x17, 0x66, 0x77, 0x05, 0x00},
        };

        object_initialize_child(OBJECT(s), "eth0", &s->greth[0], TYPE_GRETH);
        // if (nd_table[1].used) {
        //  qemu_check_nic_model(&nd_table[1], TYPE_GRETH);
        //  qdev_set_nic_properties(DEVICE(&s->greth[0]), &nd_table[1]);
        // }
        greth_change_address_space(&s->greth[0], axi_addr_space, &error_fatal);
        qdev_prop_set_macaddr(DEVICE(&s->greth[0]), "edcl_mac", edcl_mac[0]);
        /* set ip 192.168.1.2 as one number */
        qdev_prop_set_uint32(DEVICE(&s->greth[0]), "edcl_ip", 0xc0a80102);
        sysbus_realize(SYS_BUS_DEVICE(&s->greth[0]), &error_fatal);
        SysBusDevice *busdev = SYS_BUS_DEVICE(&s->greth[0]);
        memory_region_add_subregion(get_system_memory(), 0x103c035000,
                                    sysbus_mmio_get_region(busdev, 0));

        object_initialize_child(OBJECT(s), "eth1", &s->greth[1], TYPE_GRETH);
        // if (nd_table[0].used) {
        //     qemu_check_nic_model(&nd_table[0], TYPE_GRETH);
        //     qdev_set_nic_properties(DEVICE(&s->greth[1]), &nd_table[0]);
        // }
        greth_change_address_space(&s->greth[1], axi_addr_space, &error_fatal);
        qdev_prop_set_macaddr(DEVICE(&s->greth[1]), "edcl_mac", edcl_mac[1]);
        /* set ip 192.168.1.3 as one number */
        qdev_prop_set_uint32(DEVICE(&s->greth[1]), "edcl_ip", 0xc0a80103);
        sysbus_realize(SYS_BUS_DEVICE(&s->greth[1]), &error_fatal);
        busdev = SYS_BUS_DEVICE(&s->greth[1]);
        memory_region_add_subregion(get_system_memory(), 0x103c036000,
                                    sysbus_mmio_get_region(busdev, 0));

        object_initialize_child(OBJECT(s), "eth2", &s->greth[2], TYPE_GRETH);
        // if (nd_table[2].used) {
        //     qemu_check_nic_model(&nd_table[2], TYPE_GRETH);
        //     qdev_set_nic_properties(DEVICE(&s->greth[2]), &nd_table[2]);
        // }
        greth_change_address_space(&s->greth[2], axi_addr_space, &error_fatal);
        qdev_prop_set_macaddr(DEVICE(&s->greth[2]), "edcl_mac", edcl_mac[2]);
        /* set ip 192.168.1.0 as one number */
        qdev_prop_set_uint32(DEVICE(&s->greth[2]), "edcl_ip", 0xc0a80100);
        sysbus_realize(SYS_BUS_DEVICE(&s->greth[2]), &error_fatal);
        busdev = SYS_BUS_DEVICE(&s->greth[2]);
        memory_region_add_subregion(get_system_memory(), 0x103c037000,
                                    sysbus_mmio_get_region(busdev, 0));

        object_initialize_child(OBJECT(s), "gbit_eth0", &s->gb_greth[0], TYPE_GRETH);
        if (nd_table[1].used) {
            qemu_check_nic_model(&nd_table[1], TYPE_GRETH);
            qdev_set_nic_properties(DEVICE(&s->gb_greth[0]), &nd_table[1]);
        }
        greth_change_address_space(&s->gb_greth[0], axi_addr_space, &error_fatal);
        qdev_prop_set_macaddr(DEVICE(&s->gb_greth[0]), "edcl_mac", edcl_mac[3]);
        /* set ip 192.168.1.49 as one number */
        qdev_prop_set_uint32(DEVICE(&s->gb_greth[0]), "edcl_ip", 0xc0a80131);
        sysbus_realize(SYS_BUS_DEVICE(&s->gb_greth[0]), &error_fatal);
        busdev = SYS_BUS_DEVICE(&s->gb_greth[0]);
        memory_region_add_subregion(get_system_memory(), 0x103c033000,
                                    sysbus_mmio_get_region(busdev, 0));

        object_initialize_child(OBJECT(s), "gbit_eth1", &s->gb_greth[1], TYPE_GRETH);
        if (nd_table[0].used) {
            qemu_check_nic_model(&nd_table[0], TYPE_GRETH);
            qdev_set_nic_properties(DEVICE(&s->gb_greth[1]), &nd_table[0]);
        }
        greth_change_address_space(&s->gb_greth[1], axi_addr_space, &error_fatal);
        qdev_prop_set_macaddr(DEVICE(&s->gb_greth[1]), "edcl_mac", edcl_mac[4]);
        /* set ip 192.168.1.48 as one number */
        qdev_prop_set_uint32(DEVICE(&s->gb_greth[1]), "edcl_ip", 0xc0a80130);
        sysbus_realize(SYS_BUS_DEVICE(&s->gb_greth[1]), &error_fatal);
        busdev = SYS_BUS_DEVICE(&s->gb_greth[1]);
        memory_region_add_subregion(get_system_memory(), 0x103c034000,
                                    sysbus_mmio_get_region(busdev, 0));
        sysbus_connect_irq(busdev, 0, qdev_get_gpio_in(DEVICE(&s->mpic), 108));
    }

    {
        // FIXME: connect gpio IRQs to corresponding MPIC IRQ lines
        for (int i = 0; i < ARRAY_SIZE(s->lsif0_mgpio); i++) {
            s->lsif0_mgpio[i] = sysbus_create_simple("pl061", 0x103c040000 + 0x1000*i, NULL);
        }

        for (int i = 0; i < ARRAY_SIZE(s->lsif1_gpio); i++) {
            s->lsif1_gpio[i] = sysbus_create_simple("pl061", 0x103c065000 + 0x1000*i, NULL);
        }

        for (int i = 0; i < ARRAY_SIZE(s->lsif1_mgpio); i++) {
            s->lsif1_mgpio[i] = sysbus_create_simple("pl061", 0x103c067000 + 0x1000*i, NULL);
        }
    }

    {
        object_initialize_child(OBJECT(s), "sdio", &s->sdio, TYPE_KEYASIC_SD);
        keyasic_sd_change_address_space(&s->sdio, axi_addr_space, &error_fatal);
        sysbus_realize(SYS_BUS_DEVICE(&s->sdio), &error_fatal);
        SysBusDevice *busdev = SYS_BUS_DEVICE(&s->sdio);
        memory_region_add_subregion(get_system_memory(), 0x103c064000,
                                    sysbus_mmio_get_region(busdev, 0));
        sysbus_connect_irq(busdev, 0, qdev_get_gpio_in(DEVICE(&s->mpic), 67));

        // Connect SD card presence (1st pin of gpio1) with SDIO controller
        qdev_connect_gpio_out_named(DEVICE(&s->sdio), "card-inserted", 0,
                              qdev_get_gpio_in(s->lsif1_gpio[1], 1));

        DriveInfo *dinfo = drive_get(IF_SD, 0, 0);
        if (dinfo) {
            DeviceState *card;

            card = qdev_new(TYPE_SD_CARD);
            qdev_prop_set_drive_err(card, "drive", blk_by_legacy_dinfo(dinfo),
                                    &error_fatal);
            qdev_prop_set_uint8(card, "spec_version", SD_PHY_SPECv3_01_VERS);
            qdev_realize_and_unref(card, qdev_get_child_bus(DEVICE(&s->sdio), "sd-bus"),
                                   &error_fatal);
        }
    }

    {
        object_initialize_child(OBJECT(s), "spi0", &s->spi[0], TYPE_PL022);
        sysbus_realize(SYS_BUS_DEVICE(&s->spi[0]), &error_fatal);
        busdev = SYS_BUS_DEVICE(&s->spi[0]);
        memory_region_add_subregion(get_system_memory(), 0x103c061000,
                                    sysbus_mmio_get_region(busdev, 0));

        DriveInfo *dinfo = drive_get(IF_MTD, 0, 0);
        if (dinfo) {
            DeviceState *flash_dev;
            struct BlockBackend *blk = blk_by_legacy_dinfo(dinfo);

            switch (blk_getlength(blk)) {
            default:
            case 4 * MiB:
                flash_dev = qdev_new("m25p32");
                break;

            case 16 * MiB:
                flash_dev = qdev_new("n25q128a13");
                break;
            }

            qdev_prop_set_drive_err(flash_dev, "drive", blk, &error_fatal);

            // Our flash has 1 dummy cycle (or at least with this value it works)
            // So we take default value and set dummy cycles to 1
            object_property_set_int(OBJECT(flash_dev), "nonvolatile-cfg", 0x1fff,
                                    &error_fatal);
            qdev_realize(flash_dev, BUS(s->spi[0].ssi), &error_fatal);

            // Connect spi_flash chip select (cs pin) to 2nd pin of gpio1
            qdev_connect_gpio_out(s->lsif1_gpio[1], 2,
                                  qdev_get_gpio_in_named(flash_dev, SSI_GPIO_CS, 0));
        }
    }

    {
        object_initialize_child(OBJECT(s), "spi1", &s->spi[1], TYPE_PL022);
        sysbus_realize(SYS_BUS_DEVICE(&s->spi[1]), &error_fatal);
        busdev = SYS_BUS_DEVICE(&s->spi[1]);
        memory_region_add_subregion(get_system_memory(), 0x103c062000,
                                    sysbus_mmio_get_region(busdev, 0));

        object_initialize_child(OBJECT(s), "spi2", &s->spi[2], TYPE_PL022);
        sysbus_realize(SYS_BUS_DEVICE(&s->spi[2]), &error_fatal);
        busdev = SYS_BUS_DEVICE(&s->spi[2]);
        memory_region_add_subregion(get_system_memory(), 0x103c063000,
                                    sysbus_mmio_get_region(busdev, 0));
    }

    MemoryRegion *BOOT_ROM_1 = g_new(MemoryRegion, 1);
    memory_region_init_rom(BOOT_ROM_1, NULL, "BOOT_ROM_1", 256 * KiB, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x1100000000, BOOT_ROM_1);

    MemoryRegion *BOOT_ROM_1_alias = g_new(MemoryRegion, 1);
    memory_region_init_alias(BOOT_ROM_1_alias, NULL, "BOOT_ROM_1_alias", BOOT_ROM_1, 0,
                             256 * KiB);
    memory_region_add_subregion(axi_mem, 0x0, BOOT_ROM_1_alias);

    /* We doesn't emulate PCI-E (yet?) so add it's memory as unimplemented */
    create_unimplemented_device("XHSIF0", 0x1200000000, 4 * GiB);
    create_unimplemented_device("XHSIF1", 0x1300000000, 4 * GiB);


    MemoryRegion *BOOT_ROM = g_new(MemoryRegion, 1);
    memory_region_init_alias(BOOT_ROM, NULL, "BOOT_ROM", BOOT_ROM_1, 0, 256 * KiB);
    memory_region_add_subregion(get_system_memory(), 0x3fffffc0000, BOOT_ROM);

    qemu_register_reset(cpu_reset_temp, s->cpu);
}

static void mm7705_reset(MachineState *machine, ShutdownCause reason)
{
    MM7705MachineState *s = MM7705_MACHINE(machine);

    // default action
    qemu_devices_reset(reason);

    // FIXME: не надо ли как-то по-другому помещать прошивку в память?
    {
        uint32_t file_size = 256 * KiB;
        uint8_t data[256 * KiB];
        int fd = open(machine->firmware, O_RDONLY);

        if (fd == -1) {
            printf("No bios file '%s' found\n", machine->firmware);
            exit(-1);
        }

        if (read(fd, data, file_size) != file_size) {
            printf("File size is less then expected %u bytes\n", file_size);
        }

        close(fd);

        address_space_write_rom(&address_space_memory, 0x3fffffc0000,
            MEMTXATTRS_UNSPECIFIED, data, file_size);
    }

    // STCL
    if (address_space_write(&address_space_memory, 0x1038000000,
            MEMTXATTRS_UNSPECIFIED, &s->boot_cfg, sizeof(s->boot_cfg)) != 0) {
        printf("shit!!1\n");
    }

    uint8_t pll_state = 0x3f;
    if (address_space_write(&address_space_memory, 0x1038000004,
            MEMTXATTRS_UNSPECIFIED, &pll_state, sizeof(pll_state)) != 0) {
        printf("shit!!2\n");
    }

    MemoryRegion *cpu_pll = g_new(MemoryRegion, 1);
    memory_region_init_io(cpu_pll, NULL, &cpu_pll_ops, NULL, "cpu_pll", 0x4);
    memory_region_add_subregion(get_system_memory(), 0x1038006000, cpu_pll);

    {
        struct ddr_phy *ddr_phy;
        ddr_phy = calloc(2, sizeof(ddr_phy));
        assert(ddr_phy != NULL);

        MemoryRegion *EM0_ddr_phy = g_new(MemoryRegion, 1);
        memory_region_init_io(EM0_ddr_phy, NULL, &ddr_phy_ops, &ddr_phy[0], "EM0_ddr_phy",
                              0x400);
        memory_region_add_subregion(get_system_memory(), 0x103800E000, EM0_ddr_phy);

        MemoryRegion *EM1_ddr_phy = g_new(MemoryRegion, 1);
        memory_region_init_io(EM1_ddr_phy, NULL, &ddr_phy_ops, &ddr_phy[1], "EM1_ddr_phy",
                              0x400);
        memory_region_add_subregion(get_system_memory(), 0x103800F000, EM1_ddr_phy);
    }

    // Set GPIO0 pins
    uint8_t boot_cfg = s->boot_cfg;
    for (int i = 0; boot_cfg; i++, boot_cfg >>= 1) {
        if (boot_cfg & (1<<i)) {
            qemu_irq_raise(qdev_get_gpio_in(s->lsif1_gpio[0], i));
        }
    }
}

static void mm7705_boot_cfg_get_and_set(Object *obj, Visitor *v, const char *name,
                                        void *opaque, Error **errp)
{
    MM7705MachineState *s = MM7705_MACHINE(obj);

    visit_type_uint8(v, name, &s->boot_cfg, errp);
}

static void mm7705_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "MB115.01 board";
    mc->alias = "mm7705";

    mc->init = mm7705_init;
    mc->reset = mm7705_reset;
    mc->default_cpu_type = POWERPC_CPU_TYPE_NAME("476fp");

    /* default mb115.01 board has 2 DDR slots with 2 GB on each */
    mc->default_ram_size = 4 * GiB;

    ObjectProperty *prop;
    prop = object_class_property_add(oc, "boot-cfg", "uint8", mm7705_boot_cfg_get_and_set,
                                     mm7705_boot_cfg_get_and_set, NULL, NULL);
    object_property_set_default_uint(prop, MM7705_BOOT_CFG_DEFVAL);
}

static const TypeInfo mm7705_info = {
    .name = TYPE_MM7705_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(MM7705MachineState),
    .class_init = mm7705_class_init,
};

static void mm7705_machines_init(void)
{
    type_register_static(&mm7705_info);
}

type_init(mm7705_machines_init)
