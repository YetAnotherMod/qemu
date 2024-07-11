#ifndef GR1553B_H
#define GR1553B_H

#include "hw/sysbus.h"
#include "qemu/thread.h"

#include <virtmko.h>

struct GR1553BState {
    /*< private >*/
    SysBusDevice parent;

    /*< public >*/
    MemoryRegion iomem;

    qemu_irq irq;

    uint32_t reg_irq; /* access using atomics */
    uint32_t reg_mask;

    uint32_t reg_bc_trans;

    /* internal */
    QemuMutex internal_mutex;
    int internal_signal; /* access locked by `internal_mutex` */
    uint32_t bc_scst; /* access locked by `internal_mutex` */

    QemuMutex bc_mutex;
    QemuThread bc_thread;

    /* virtmko */
    vmko_controller *vmko_ctrl;
    QemuMutex bc_recv_wait;
    vmko_msg resp;
    bool resp_valid;
};

typedef struct GR1553BState GR1553BState;

#define TYPE_GR1553B "gr1553b"
#define GR1553B(obj) OBJECT_CHECK(GR1553BState, (obj), TYPE_GR1553B)

#endif
