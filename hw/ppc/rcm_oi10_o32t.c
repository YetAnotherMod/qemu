#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "exec/address-spaces.h"
#include "sysemu/reset.h"
#include "hw/sysbus.h"
#include "hw/ppc/ppc.h"
#include "cpu.h"
#include "hw/ppc/dcr_mpic.h"
#include "hw/char/pl011.h"
#include "sysemu/sysemu.h"
#include "hw/sd/keyasic_sd.h"
#include "hw/ppc/plb6_dma.h"
#include "hw/net/greth.h"
#include "net/eth.h"
#include "hw/misc/commport.h"

#ifdef CONFIG_VIRTSW
#include "hw/misc/rcm_spacewire.h"
#endif

#ifdef CONFIG_VIRTMKO
#include "hw/misc/gr1553b.h"
#endif

#include "hw/ppc/rcm_oi10_o32t.h"

/* Abstract class definition
 */
#define TYPE_OI10_O32T "oi10_o32t"
OBJECT_DECLARE_TYPE(Oi10O32tState, Oi10O32tClass, OI10_O32T)

struct Oi10O32tClass {
    SysBusDeviceClass parent_class;
    bool is_o32t;
};

/* OI10 and O32T structs
 */
#define UART_COUNT 2
#define GPIO_COUNT 2
#define GRETH_COUNT 2
#define SW_COUNT 4
#define MKO_COUNT_OI10 2
#define MKO_COUNT_O32T 4
#define MKO_COUNT_MAX MKO_COUNT_O32T
#define COMM_COUNT 2

struct Oi10O32tState {
    /*< private >*/
    SysBusDevice parent;

    /*< public >*/
    PowerPCCPU *cpu;

    /* DCR bus */
    MpicState mpic;
    PLB6DMAState plb6dma;

    /* PLB6 bus */
    MemoryRegion *EMI;
    PL011State uart[UART_COUNT];
    DeviceState *gpio[GPIO_COUNT];
    GRETHState greth[GRETH_COUNT];
    KeyasicSdState sdio;

#ifdef CONFIG_VIRTSW
    RCMSpaceWireState sw[SW_COUNT];
#endif

#ifdef CONFIG_VIRTMKO
    GR1553BState mko[MKO_COUNT_MAX];
#endif

    /* boot properties */
    uint8_t boot_cfg;
    /* firmware path */
    char *firmware;
};

struct OI10State {
    /*< private >*/
    Oi10O32tState parent;
};

struct O32TState {
    /*< private >*/
    Oi10O32tState parent;

    CommState comm[COMM_COUNT];
};

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

static uint32_t plb4arb8m_dcr_read(void *opaque, int dcrn)
{
    return 0;
}

static void plb4arb8m_dcr_write(void *opaque, int dcrn, uint32_t val)
{
}

static void dcr_plb4arb8m_register(CPUPPCState *env, uint32_t base)
{
    ppc_dcr_register(env, base + 0x2, NULL, plb4arb8m_dcr_read,
                     plb4arb8m_dcr_write);
    ppc_dcr_register(env, base + 0x3, NULL, plb4arb8m_dcr_read,
                     plb4arb8m_dcr_write);
    ppc_dcr_register(env, base + 0x4, NULL, plb4arb8m_dcr_read,
                     plb4arb8m_dcr_write);
    ppc_dcr_register(env, base + 0x6, NULL, plb4arb8m_dcr_read,
                     plb4arb8m_dcr_write);
    ppc_dcr_register(env, base + 0x7, NULL, plb4arb8m_dcr_read,
                     plb4arb8m_dcr_write);
}

static uint32_t p6bc_dcr_read(void *opaque, int dcrn)
{
    return 0;
}

static void p6bc_dcr_write(void *opaque, int dcrn, uint32_t val)
{
}

static void dcr_p6bc_register(CPUPPCState *env, uint32_t base)
{
    uint32_t i;

    for (i = 0; i <= 0x11; i++) {
        ppc_dcr_register(env, base + i, NULL, p6bc_dcr_read, p6bc_dcr_write);
    }
}

static uint32_t dcrarb_dcr_read(void *opaque, int dcrn)
{
    return 0;
}

static void dcrarb_dcr_write(void *opaque, int dcrn, uint32_t val)
{
}

static void dcr_dcrarb_register(CPUPPCState *env, uint32_t base)
{
    uint32_t i;

    for (i = 0; i <= 0x7; i++) {
        ppc_dcr_register(env, base + i, NULL, dcrarb_dcr_read,
                         dcrarb_dcr_write);
    }
}

static uint32_t ddr_mclfir_dcr_read(void *opaque, int dcrn)
{
    return 0;
}

static void ddr_mclfir_dcr_write(void *opaque, int dcrn, uint32_t val)
{
}

static void dcr_ddr_mclfir_register(CPUPPCState *env, uint32_t base)
{
    uint32_t i;

    for (i = 0x0; i <= 0x35; i++) {
        ppc_dcr_register(env, base + i, NULL, ddr_mclfir_dcr_read,
                         ddr_mclfir_dcr_write);
    }
}

static uint32_t ddr_plb6mcif2_dcr_read(void *opaque, int dcrn)
{
    if (dcrn == 0x80050020) {
        return 0x80000000;
    }

    return 0;
}

static void ddr_plb6mcif2_dcr_write(void *opaque, int dcrn, uint32_t val)
{
}

static void dcr_ddr_plb6mcif2_register(CPUPPCState *env, uint32_t base)
{
    uint32_t i;

    for (i = 0x0; i <= 0x3f; i++) {
        ppc_dcr_register(env, base + i, NULL, ddr_plb6mcif2_dcr_read,
                         ddr_plb6mcif2_dcr_write);
    }
}

static uint32_t sctl_dcr_read(void *opaque, int dcrn)
{
    Oi10O32tState *s = opaque;

    if (dcrn == 0x80090000) {
        return s->boot_cfg;
    }

    return 0;
}

static void sctl_dcr_write(void *opaque, int dcrn, uint32_t val)
{
}

static void dcr_sctl_register(CPUPPCState *env, uint32_t base, void *opaque)
{
    uint32_t i;

    for (i = 0x0; i <= 0x100; i++) {
        ppc_dcr_register(env, base + i, opaque, sctl_dcr_read, sctl_dcr_write);
    }
}

static uint32_t dcr_unknown_read(void *opaque, int dcrn)
{
    return 0;
}

static void dcr_unknown_write(void *opaque, int dcrn, uint32_t val)
{
}

static void dcr_unknown16(CPUPPCState *env, uint32_t base)
{
    uint32_t i;

    for (i = 0x0; i < 0x10; i++) {
        ppc_dcr_register(env, base + i, NULL, dcr_unknown_read,
                         dcr_unknown_write);
    }
}

static void dcr_unknown256(CPUPPCState *env, uint32_t base)
{
    uint32_t i;

    for (i = 0x0; i < 0x100; i++) {
        ppc_dcr_register(env, base + i, NULL, dcr_unknown_read,
                         dcr_unknown_write);
    }
}

static void dcr_unknown64k(CPUPPCState *env, uint32_t base)
{
    uint32_t i;

    for (i = 0x0; i < 0x10000; i++) {
        ppc_dcr_register(env, base + i, NULL, dcr_unknown_read,
                         dcr_unknown_write);
    }
}

#ifdef CONFIG_VIRTMKO
static void add_mko_controllers(Oi10O32tState *s, int count)
{
    hwaddr addr[MKO_COUNT_MAX] = {0x20c0020000u, 0x20c0030000u, 0x20c0021000u,
                                  0x20c0031000u};
    int irq_line[MKO_COUNT_MAX] = {38, 39, 54, 55};
    char name[8];

    for (uint32_t i = 0; i < count; i++) {
        snprintf(name, sizeof(name), "mko[%u]", i);
        object_initialize_child(OBJECT(s), name, &s->mko[i], TYPE_GR1553B);
        sysbus_realize(SYS_BUS_DEVICE(&s->mko[i]), &error_fatal);
        SysBusDevice *busdev = SYS_BUS_DEVICE(&s->mko[i]);
        memory_region_add_subregion(get_system_memory(), addr[i],
                                    sysbus_mmio_get_region(busdev, 0));
        sysbus_connect_irq(busdev, 0,
                           qdev_get_gpio_in(DEVICE(&s->mpic), irq_line[i]));
    }
}
#else
static void add_mko_controllers(Oi10O32tState *s, int count)
{
    hwaddr addr[MKO_COUNT_MAX] = {0x20c0020000u, 0x20c0030000u, 0x20c0021000u,
                                  0x20c0031000u};
    char name[8];

    for (uint32_t i = 0; i < count; i++) {
        snprintf(name, sizeof(name), "mko[%u]", i);
        MemoryRegion *mko = g_new(MemoryRegion, 1);
        memory_region_init_ram(mko, NULL, name, 4 * KiB, &error_fatal);
        memory_region_add_subregion(get_system_memory(), addr[i], mko);
    }
}
#endif

#ifdef CONFIG_VIRTSW
static void add_spacewire_controllers(Oi10O32tState *s,
                                      AddressSpace *addr_space)
{
    const hwaddr addr[SW_COUNT] = {0x20c0300000u, 0x20c0301000u, 0x20c0302000u,
                                   0x20c0303000u};
    const int irq_line[SW_COUNT][2] = {
        {44, 45},
        {46, 47},
        {48, 49},
        {50, 51},
    };
    char name[8];

    for (uint32_t i = 0; i < SW_COUNT; i++) {
        snprintf(name, sizeof(name), "sw[%u]", i);
        object_initialize_child(OBJECT(s), name, &s->sw[i], TYPE_RCM_SPACEWIRE);
        rcm_sw_change_address_space(&s->sw[i], addr_space, &error_fatal);
        sysbus_realize(SYS_BUS_DEVICE(&s->sw[i]), &error_fatal);
        SysBusDevice *busdev = SYS_BUS_DEVICE(&s->sw[i]);
        memory_region_add_subregion(get_system_memory(), addr[i],
                                    sysbus_mmio_get_region(busdev, 0));
        sysbus_connect_irq(busdev, 0,
                           qdev_get_gpio_in(DEVICE(&s->mpic), irq_line[i][0]));
        sysbus_connect_irq(busdev, 1,
                           qdev_get_gpio_in(DEVICE(&s->mpic), irq_line[i][1]));
    }
}
#else
static void add_spacewire_controllers(Oi10O32tState *s,
                                      AddressSpace *addr_space)
{
    const hwaddr addr[SW_COUNT] = {0x20c0300000u, 0x20c0301000u, 0x20c0302000u,
                                   0x20c0303000u};
    char name[8];

    (void)addr_space; /* unused */

    for (uint32_t i = 0; i < SW_COUNT; i++) {
        snprintf(name, sizeof(name), "sw[%u]", i);
        MemoryRegion *mko = g_new(MemoryRegion, 1);
        memory_region_init_ram(mko, NULL, name, 4 * KiB, &error_fatal);
        memory_region_add_subregion(get_system_memory(), addr[i], mko);
    }
}
#endif

static void add_communication_ports(Oi10O32tState *s, AddressSpace *addr_space)
{
    SysBusDevice *busdev;
    O32TState *s_o32t = O32T(s);

    if (qemu_chr_find("NMCOMM0")) {
        object_initialize_child(OBJECT(s_o32t), "comm0", &s_o32t->comm[0],
                                TYPE_COMM);
        comm_change_address_space(&s_o32t->comm[0], addr_space, &error_fatal);
        qdev_prop_set_chr(DEVICE(&s_o32t->comm[0]), "CommChardev",
                          qemu_chr_find("NMCOMM0"));
        qdev_prop_set_uint8(DEVICE(&s_o32t->comm[0]), "RegistersMode", 1);
        sysbus_realize(SYS_BUS_DEVICE(&s_o32t->comm[0]), &error_fatal);
        busdev = SYS_BUS_DEVICE(&s_o32t->comm[0]);
        memory_region_add_subregion(get_system_memory(), 0x20c0304000,
                                    sysbus_mmio_get_region(busdev, 0));
        sysbus_connect_irq(busdev, 0, qdev_get_gpio_in(DEVICE(&s->mpic), 79));
        sysbus_connect_irq(busdev, 1, qdev_get_gpio_in(DEVICE(&s->mpic), 78));
    }

    if (qemu_chr_find("NMCOMM1")) {
        object_initialize_child(OBJECT(s_o32t), "comm1", &s_o32t->comm[1],
                                TYPE_COMM);
        comm_change_address_space(&s_o32t->comm[1], addr_space, &error_fatal);
        qdev_prop_set_chr(DEVICE(&s_o32t->comm[1]), "CommChardev",
                          qemu_chr_find("NMCOMM1"));
        qdev_prop_set_uint8(DEVICE(&s_o32t->comm[1]), "RegistersMode", 1);
        sysbus_realize(SYS_BUS_DEVICE(&s_o32t->comm[1]), &error_fatal);
        busdev = SYS_BUS_DEVICE(&s_o32t->comm[1]);
        memory_region_add_subregion(get_system_memory(), 0x20c0305000,
                                    sysbus_mmio_get_region(busdev, 0));
        sysbus_connect_irq(busdev, 0, qdev_get_gpio_in(DEVICE(&s->mpic), 81));
        sysbus_connect_irq(busdev, 1, qdev_get_gpio_in(DEVICE(&s->mpic), 80));
    }
}

static void oi10_o32t_sdio_card_inserted(void *opaque, int n, int level)
{
    Oi10O32tState *s = OI10_O32T(opaque);

    // set boot_cfg 3rd bit according to "card-inserted" state
    if (level) {
        s->boot_cfg |= 1u << OI10_O32T_SD_CARD_INSERTED_GPIO;
    } else {
        s->boot_cfg &= ~(1u << OI10_O32T_SD_CARD_INSERTED_GPIO);
    }

    qemu_set_irq(qdev_get_gpio_in(s->gpio[0], OI10_O32T_SD_CARD_INSERTED_GPIO),
                 level);
}

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

static void oi10_o32t_cpu_reset(void *opaque)
{
    PowerPCCPU *cpu = opaque;

    cpu_reset(CPU(cpu));

    /* Create mapping */
    create_initial_mapping(&cpu->env);
}

static void oi10_o32t_realize(DeviceState *dev, Error **errp)
{
    Oi10O32tState *s = OI10_O32T(dev);
    Oi10O32tClass *class = OI10_O32T_GET_CLASS(dev);

    // FIXME: maybe put this into the machine code?
    const uint32_t cpu_freq = 200 * 1000 * 1000;

    /* init CPUs */
    s->cpu = POWERPC_CPU(cpu_create(POWERPC_CPU_TYPE_NAME("476fp")));
    ppc_booke_timers_init(s->cpu, cpu_freq, 0);

    /* DCR bus */
    CPUPPCState *env = &s->cpu->env;
    ppc_dcr_init(env, dcr_read_error, dcr_write_error);

    dcr_plb4arb8m_register(env, 0x00000010);

    if (class->is_o32t) {
        dcr_plb4arb8m_register(env, 0x00000060);
    } else {
        dcr_plb4arb8m_register(env, 0x00000020);
    }

    dcr_unknown16(env, 0x00000030);
    dcr_unknown16(env, 0x00000040);
    dcr_unknown16(env, 0x00000050);

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
    object_property_set_int(OBJECT(&s->mpic), "baseaddr", 0xffc00000,
                            &error_fatal);
    object_property_set_int(OBJECT(&s->mpic), "timer-freq", cpu_freq / 8,
                            &error_fatal);
    object_property_set_link(OBJECT(&s->mpic), "cpu-state", OBJECT(s->cpu),
                             &error_fatal);
    qdev_realize(DEVICE(&s->mpic), NULL, &error_fatal);
    qdev_connect_gpio_out_named(
        DEVICE(&s->mpic), "non_crit_int", 0,
        qdev_get_gpio_in(DEVICE(s->cpu), PPC40x_INPUT_INT));
    qdev_connect_gpio_out_named(
        DEVICE(&s->mpic), "crit_int", 0,
        qdev_get_gpio_in(DEVICE(s->cpu), PPC40x_INPUT_CINT));

    object_initialize_child(OBJECT(s), "dmaplb6", &s->plb6dma, TYPE_PLB6_DMA);
    object_property_set_int(OBJECT(&s->plb6dma), "baseaddr", 0x80000100,
                            &error_fatal);
    object_property_set_link(OBJECT(&s->plb6dma), "cpu-state", OBJECT(s->cpu),
                             &error_fatal);
    qdev_realize(DEVICE(&s->plb6dma), NULL, &error_fatal);
    for (uint32_t i = 0; i < NUMBER_OF_IRQS; i++) {
        qdev_connect_gpio_out(DEVICE(&s->plb6dma), i,
                              qdev_get_gpio_in(DEVICE(&s->mpic), 3 + i));
    }

    /* PLB6 bus */
    /* Board has separated AXI bus for peripherial devices */
    MemoryRegion *axi_mem = g_new(MemoryRegion, 1);
    AddressSpace *axi_addr_space = g_new(AddressSpace, 1);
    memory_region_init(axi_mem, NULL, "axi_mem", ~0u);
    address_space_init(axi_addr_space, axi_mem, "axi_addr_space");

    s->EMI = g_new(MemoryRegion, 1);
    memory_region_init_ram(s->EMI, NULL, "EMI", 2 * GiB, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x0, s->EMI);

    MemoryRegion *EMI_on_AXI = g_new(MemoryRegion, 1);
    memory_region_init_alias(EMI_on_AXI, NULL, "EMI_on_AXI", s->EMI, 0, 2 * GiB);
    memory_region_add_subregion(axi_mem, 0x0, EMI_on_AXI);

    MemoryRegion *IM0 = g_new(MemoryRegion, 1);
    memory_region_init_ram(IM0, NULL, "IM0", 128 * KiB, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x1080000000, IM0);

    MemoryRegion *rom = g_new(MemoryRegion, 1);
    memory_region_init_rom(rom, NULL, "rom", 64 * KiB, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x1fffff0000, rom);

    MemoryRegion *IM1 = g_new(MemoryRegion, 1);
    memory_region_init_ram(IM1, NULL, "IM1", 128 * KiB, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x20c0000000, IM1);

    MemoryRegion *IM1_on_AXI = g_new(MemoryRegion, 1);
    memory_region_init_alias(IM1_on_AXI, NULL, "IM1_on_AXI", IM1, 0, 128 * KiB);
    memory_region_add_subregion(axi_mem, 0xc0000000, IM1_on_AXI);

    add_mko_controllers(s, class->is_o32t ? MKO_COUNT_O32T : MKO_COUNT_OI10);

    s->gpio[0] = sysbus_create_simple("pl061", 0x20c0028000,
                                      qdev_get_gpio_in(DEVICE(&s->mpic), 32));

    SysBusDevice *busdev;
    if (serial_hd(0)) {
        object_initialize_child(OBJECT(s), "uart0", &s->uart[0], TYPE_PL011);
        qdev_prop_set_chr(DEVICE(&s->uart[0]), "chardev", serial_hd(0));
        sysbus_realize(SYS_BUS_DEVICE(&s->uart[0]), &error_fatal);
        busdev = SYS_BUS_DEVICE(&s->uart[0]);
        memory_region_add_subregion(get_system_memory(), 0x20c0029000,
                                    sysbus_mmio_get_region(busdev, 0));
        sysbus_connect_irq(busdev, 0, qdev_get_gpio_in(DEVICE(&s->mpic), 36));
    }

    const uint8_t edcl_mac[][GRETH_COUNT][ETH_ALEN] = {
        /* is_o32t = 0 */
        [0] =
            {
                {0xec, 0x17, 0x66, 0x0e, 0x10, 0x00},
                {0xec, 0x17, 0x66, 0x0e, 0x10, 0x01},
            },
        /* is_o32t = 1 */
        [1] =
            {
                {0xec, 0x17, 0x66, 0xed, 0xc1, 0x01},
                {0xec, 0x17, 0x66, 0xed, 0xc1, 0x02},
            },
    };

    const uint32_t edcl_ip[][GRETH_COUNT] = {
        /* is_o32t = 0 */
        [0] =
            {
                0xc0a80130, /* 192.168.1.48 */
                0xc0a80131, /* 192.168.1.49 */
            },
        /* is_o32t = 1 */
        [1] =
            {
                0xc0a80321, /* 192.168.3.33 */
                0xc0a80322, /* 192.168.3.34 */
            },
    };

    object_initialize_child(OBJECT(s), "eth0", &s->greth[0], TYPE_GRETH);
    if (nd_table[0].used) {
        qemu_check_nic_model(&nd_table[0], TYPE_GRETH);
        qdev_set_nic_properties(DEVICE(&s->greth[0]), &nd_table[0]);
    }
    greth_change_address_space(&s->greth[0], axi_addr_space, &error_fatal);
    qdev_prop_set_macaddr(DEVICE(&s->greth[0]), "edcl_mac",
                          edcl_mac[class->is_o32t][0]);

    qdev_prop_set_uint32(DEVICE(&s->greth[0]), "edcl_ip",
                         edcl_ip[class->is_o32t][0]);
    qdev_prop_set_uint32(DEVICE(&s->greth[0]), "edcl_disabled", 0);
    sysbus_realize(SYS_BUS_DEVICE(&s->greth[0]), &error_fatal);
    busdev = SYS_BUS_DEVICE(&s->greth[0]);
    memory_region_add_subregion(get_system_memory(), 0x20c002a000,
                                sysbus_mmio_get_region(busdev, 0));
    sysbus_connect_irq(busdev, 0, qdev_get_gpio_in(DEVICE(&s->mpic), 52));

    MemoryRegion *spi0 = g_new(MemoryRegion, 1);
    memory_region_init_ram(spi0, NULL, "spi0", 4 * KiB, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x20c002b000, spi0);

    object_initialize_child(OBJECT(s), "sdio", &s->sdio, TYPE_KEYASIC_SD);
    keyasic_sd_change_address_space(&s->sdio, axi_addr_space, &error_fatal);
    sysbus_realize(SYS_BUS_DEVICE(&s->sdio), &error_fatal);
    busdev = SYS_BUS_DEVICE(&s->sdio);
    memory_region_add_subregion(get_system_memory(), 0x20c002c000,
                                sysbus_mmio_get_region(busdev, 0));
    sysbus_connect_irq(busdev, 0, qdev_get_gpio_in(DEVICE(&s->mpic), 34));

    // Connect SD card presence (3rd pin of gpio0) with SDIO controller
    qdev_connect_gpio_out_named(
        DEVICE(&s->sdio), "card-inserted", 0,
        qemu_allocate_irq(oi10_o32t_sdio_card_inserted, s, 1));

    s->gpio[1] = sysbus_create_simple("pl061", 0x20c0038000,
                                      qdev_get_gpio_in(DEVICE(&s->mpic), 33));

    if (serial_hd(1)) {
        object_initialize_child(OBJECT(s), "uart1", &s->uart[1], TYPE_PL011);
        qdev_prop_set_chr(DEVICE(&s->uart[1]), "chardev", serial_hd(1));
        sysbus_realize(SYS_BUS_DEVICE(&s->uart[1]), &error_fatal);
        busdev = SYS_BUS_DEVICE(&s->uart[1]);
        memory_region_add_subregion(get_system_memory(), 0x20c0039000,
                                    sysbus_mmio_get_region(busdev, 0));
        sysbus_connect_irq(busdev, 0, qdev_get_gpio_in(DEVICE(&s->mpic), 37));
    }

    object_initialize_child(OBJECT(s), "eth1", &s->greth[1], TYPE_GRETH);
    greth_change_address_space(&s->greth[1], axi_addr_space, &error_fatal);
    qdev_prop_set_macaddr(DEVICE(&s->greth[1]), "edcl_mac",
                          edcl_mac[class->is_o32t][1]);
    /* set ip 192.168.1.49 as one number */
    qdev_prop_set_uint32(DEVICE(&s->greth[1]), "edcl_ip",
                         edcl_ip[class->is_o32t][1]);
    qdev_prop_set_uint32(DEVICE(&s->greth[1]), "edcl_disabled", 0);
    sysbus_realize(SYS_BUS_DEVICE(&s->greth[1]), &error_fatal);
    busdev = SYS_BUS_DEVICE(&s->greth[1]);
    memory_region_add_subregion(get_system_memory(), 0x20c003a000,
                                sysbus_mmio_get_region(busdev, 0));
    sysbus_connect_irq(busdev, 0, qdev_get_gpio_in(DEVICE(&s->mpic), 53));

    MemoryRegion *spi1 = g_new(MemoryRegion, 1);
    memory_region_init_ram(spi1, NULL, "spi1", 4 * KiB, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x20c003b000, spi1);

    MemoryRegion *sdio1 = g_new(MemoryRegion, 1);
    memory_region_init_ram(sdio1, NULL, "sdio1", 4 * KiB, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x20c003c000, sdio1);

    MemoryRegion *IM2 = g_new(MemoryRegion, 1);
    memory_region_init_ram(IM2, NULL, "IM2", 128 * KiB, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x20c0040000, IM2);

    MemoryRegion *IM2_on_AXI = g_new(MemoryRegion, 1);
    memory_region_init_alias(IM2_on_AXI, NULL, "IM2_on_AXI", IM2, 0, 128 * KiB);
    memory_region_add_subregion(axi_mem, 0xc0040000, IM2_on_AXI);

    if (class->is_o32t) {
        MemoryRegion *IM3 = g_new(MemoryRegion, 1);
        memory_region_init_ram(IM3, NULL, "IM3", 128 * KiB, &error_fatal);
        memory_region_add_subregion(get_system_memory(), 0x20c0060000, IM3);

        MemoryRegion *IM3_on_AXI = g_new(MemoryRegion, 1);
        memory_region_init_alias(IM3_on_AXI, NULL, "IM3_on_AXI", IM3, 0,
                                 128 * KiB);
        memory_region_add_subregion(axi_mem, 0xc0060000, IM3_on_AXI);
    }

    MemoryRegion *switch_axi32l = g_new(MemoryRegion, 1);
    memory_region_init_ram(switch_axi32l, NULL, "switch_axi32l", 1 * MiB,
                           &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x20c0100000,
                                switch_axi32l);

    MemoryRegion *switch_axi32r = g_new(MemoryRegion, 1);
    memory_region_init_ram(switch_axi32r, NULL, "switch_axi32r", 1 * MiB,
                           &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x20c0200000,
                                switch_axi32r);

    add_spacewire_controllers(s, axi_addr_space);

    if (class->is_o32t) {
        add_communication_ports(s, axi_addr_space);

        MemoryRegion *AXI_DMA = g_new(MemoryRegion, 1);
        memory_region_init_ram(AXI_DMA, NULL, "AXI_DMA", 4 * KiB, &error_fatal);
        memory_region_add_subregion(get_system_memory(), 0x20c0306000, AXI_DMA);

        MemoryRegion *SCRB = g_new(MemoryRegion, 1);
        memory_region_init_ram(SCRB, NULL, "SCRB", 4 * KiB, &error_fatal);
        memory_region_add_subregion(get_system_memory(), 0x20c0307000, SCRB);

        MemoryRegion *switch_axi64 = g_new(MemoryRegion, 1);
        memory_region_init_ram(switch_axi64, NULL, "switch_axi64", 1 * MiB,
                               &error_fatal);
        memory_region_add_subregion(get_system_memory(), 0x20c0400000,
                                    switch_axi64);
    }

    MemoryRegion *rom_alias = g_new(MemoryRegion, 1);
    if (s->boot_cfg & (1 << OI10_O32T_USE_INTERNAL_ROM)) {
        memory_region_init_alias(rom_alias, NULL, NULL, rom, 0, 64 * KiB);
    } else {
        memory_region_init_alias(rom_alias, NULL, NULL, s->EMI, 0x7fff0000,
                                 64 * KiB);
    }
    memory_region_add_subregion(get_system_memory(), 0x3ffffff0000, rom_alias);

    qemu_register_reset(oi10_o32t_cpu_reset, s->cpu);
}

static void oi10_o32t_reset(DeviceState *dev)
{
    Oi10O32tState *s = OI10_O32T(dev);

    // FIXME: не надо ли как-то по-другому помещать прошивку в память?
    {
        uint32_t file_size = 64 * KiB;
        uint8_t data[64 * KiB];
        int fd = open(s->firmware, O_RDONLY);

        if (fd == -1) {
            printf("No bios file '%s' found\n", s->firmware);
            exit(-1);
        }

        if (read(fd, data, file_size) != file_size) {
            printf("File size is less then expected %u bytes\n", file_size);
        }

        close(fd);

        address_space_write_rom(&address_space_memory, 0x1fffff0000,
                                MEMTXATTRS_UNSPECIFIED, data, file_size);
    }

    // Set GPIO0 pins
    uint8_t boot_cfg = s->boot_cfg;
    for (int i = 0; boot_cfg; i++, boot_cfg >>= 1) {
        if (boot_cfg & 1) {
            qemu_irq_raise(qdev_get_gpio_in(s->gpio[0], i));
        }
    }
}

MemoryRegion *oi10_o32t_get_ext_mem_region(DeviceState *dev)
{
    Oi10O32tState *s = OI10_O32T(dev);
    return s->EMI;
}

BusState *oi10_o32t_get_sdio_bus(DeviceState *dev, int sdio_num)
{
    Oi10O32tState *s = OI10_O32T(dev);
    g_assert(sdio_num == 0);
    return qdev_get_child_bus(DEVICE(&s->sdio), "sd-bus");
}

static void oi10_o32t_boot_cfg_get_and_set(Object *obj, Visitor *v,
                                           const char *name, void *opaque,
                                           Error **errp)
{
    Oi10O32tState *s = OI10_O32T(obj);

    visit_type_uint8(v, name, &s->boot_cfg, errp);
}

static void oi10_o32t_firmware_set(Object *obj, const char *name, Error **errp)
{
    Oi10O32tState *s = OI10_O32T(obj);

    s->firmware = g_strdup(name);
}

static void oi10_o32t_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    Oi10O32tClass *class = OI10_O32T_CLASS(klass);

    class->is_o32t = GPOINTER_TO_INT(data);

    set_bit(DEVICE_CATEGORY_CPU, dc->categories);
    if (class->is_o32t) {
        dc->desc = "RC Module SoC 1888BM028A";
    } else {
        dc->desc = "RC Module SoC 1888BM018";
    }
    dc->realize = oi10_o32t_realize;
    dc->reset = oi10_o32t_reset;

    object_class_property_add(klass, "boot-cfg", "uint8",
                              oi10_o32t_boot_cfg_get_and_set,
                              oi10_o32t_boot_cfg_get_and_set, NULL, NULL);
    object_class_property_add_str(klass, "firmware", NULL,
                                  oi10_o32t_firmware_set);
}

static const TypeInfo oi10_o32t_info = {
    .name = TYPE_OI10_O32T,
    .parent = TYPE_SYS_BUS_DEVICE,
    .class_size = sizeof(Oi10O32tClass),
    .abstract = true,
};

static const TypeInfo oi10_info = {
    .name = TYPE_OI10,
    .parent = TYPE_OI10_O32T,
    .instance_size = sizeof(OI10State),
    .class_init = oi10_o32t_class_init,
    .class_data = GINT_TO_POINTER(0),
};

static const TypeInfo o32t_info = {
    .name = TYPE_O32T,
    .parent = TYPE_OI10_O32T,
    .instance_size = sizeof(O32TState),
    .class_init = oi10_o32t_class_init,
    .class_data = GINT_TO_POINTER(1),
};

static void oi10_o32t_register_type(void)
{
    type_register_static(&oi10_o32t_info);
    type_register_static(&oi10_info);
    type_register_static(&o32t_info);
}

type_init(oi10_o32t_register_type)
