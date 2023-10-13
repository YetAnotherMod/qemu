#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/qemu-print.h"
#include "cpu.h"

static void nmc_cpu_initfn(Object *obj)
{
    NMCCPU *cpu = NMC_CPU(obj);

    cpu_set_cpustate_pointers(cpu);
}

static void nmc_cpu_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    CPUClass *cc = CPU_CLASS(oc);
    NMCCPUClass *mcc = NMC_CPU_CLASS(oc);

    device_class_set_parent_realize(dc, avr_cpu_realizefn, &mcc->parent_realize);
    device_class_set_parent_reset(dc, avr_cpu_reset, &mcc->parent_reset);

    cc->class_by_name = avr_cpu_class_by_name;

    cc->has_work = avr_cpu_has_work;
    cc->dump_state = avr_cpu_dump_state;
    cc->set_pc = avr_cpu_set_pc;
    cc->memory_rw_debug = avr_cpu_memory_rw_debug;
    dc->vmsd = &vms_avr_cpu;
    cc->sysemu_ops = &avr_sysemu_ops;
    cc->disas_set_info = avr_cpu_disas_set_info;
    cc->gdb_read_register = avr_cpu_gdb_read_register;
    cc->gdb_write_register = avr_cpu_gdb_write_register;
    cc->gdb_adjust_breakpoint = avr_cpu_gdb_adjust_breakpoint;
    cc->gdb_num_core_regs = 35;
    cc->gdb_core_xml_file = "avr-cpu.xml";
    cc->tcg_ops = &avr_tcg_ops;
}

void nmc_cpu_list(void)
{
	qemu_printf("nmc-cpu\n");
}

static const TypeInfo nmc_cpu_type_info[] = {
    {
        .name = TYPE_NMC_CPU,
        .parent = TYPE_CPU,
        .instance_size = sizeof(NMCCPU),
        .instance_init = nmc_cpu_initfn,
        .class_size = sizeof(NMCCPUClass),
        .class_init = nmc_cpu_class_init,
    }
};

DEFINE_TYPES(nmc_cpu_type_info)
