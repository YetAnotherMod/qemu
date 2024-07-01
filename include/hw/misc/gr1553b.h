#ifndef GR1553B_H
#define GR1553B_H

#include "hw/sysbus.h"

struct GR1553BState {
    /*< private >*/
    SysBusDevice parent;

    /*< public >*/
    MemoryRegion iomem;

    qemu_irq irq;

    uint32_t reg_irq;
    uint32_t reg_mask;

    uint32_t reg_bc_act;
    uint32_t reg_bc_trans;
};

typedef struct GR1553BState GR1553BState;

#define TYPE_GR1553B "gr1553b"
#define GR1553B(obj) OBJECT_CHECK(GR1553BState, (obj), TYPE_GR1553B)

#endif
