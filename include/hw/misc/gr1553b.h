#ifndef GR1553B_H
#define GR1553B_H

#include "hw/sysbus.h"
#include "qemu/thread.h"
#include "net/net.h"

#include <virtmko_logic.h>

struct GR1553BState {
    /*< private >*/
    SysBusDevice parent;

    /* Address space for internal DMA that can be changed during board init */
    AddressSpace *addr_space;

    /*< public >*/
    MemoryRegion iomem;

    qemu_irq irq;

    uint32_t reg_irq; /* access using atomics */
    uint32_t reg_mask;

    uint32_t reg_bc_trans;
    uint32_t reg_bc_tt_irq_ring_pos; /* access locked by `internal_mutex` */
    uint32_t reg_bc_async_list_next_ptr;

    uint32_t reg_rt_bus_status;
    uint32_t reg_rt_subaddr_base_addr;

    /* internal */
    QemuMutex internal_mutex;
    int internal_signal; /* access locked by `internal_mutex` */
    uint32_t bc_scst; /* access locked by `internal_mutex` */
    uint32_t bc_tt_irq_ring_offset; /* access locked by `internal_mutex` */
    uint32_t rt_addr;
    uint32_t rt_enabled;

    QemuMutex bc_mutex;
    QemuThread bc_thread;

    /* virtmko */
    vmko_logic_t *vmko_logic;
    GQueue bc_recv_queue;
    QemuMutex bc_recv_queue_mutex;
    QemuSemaphore bc_recv_queue_sem;
    vmko_msg resp;
    bool resp_valid;

    NICConf nicconf;
    NICState *vmko_nic;
};

typedef struct GR1553BState GR1553BState;

#define TYPE_GR1553B "gr1553b"
#define GR1553B(obj) OBJECT_CHECK(GR1553BState, (obj), TYPE_GR1553B)

void gr1553b_change_address_space(GR1553BState *s, AddressSpace *addr_space,
                                  Error **errp);

#endif
