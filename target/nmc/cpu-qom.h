#ifndef QEMU_NMC_QOM_H
#define QEMU_NMC_QOM_H

#include "hw/core/cpu.h"
#include "qom/object.h"

#define TYPE_NMC_CPU "nmc-cpu"

OBJECT_DECLARE_TYPE(NMCCPU, NMCCPUClass,
                    NMC_CPU)

struct NMCCPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/
    DeviceRealize parent_realize;
    DeviceReset parent_reset;
};

#endif
