#ifndef RCM_SPACEWIRE_H
#define RCM_SPACEWIRE_H

#include "hw/sysbus.h"
#include "sysemu/dma.h"

#include "virtsw.h"

struct RCMSpaceWireState {
    /*< private >*/
    SysBusDevice parent;

    /* Address space for internal DMA that can be changed during board init */
    AddressSpace *addr_space;

    /*< public >*/
    MemoryRegion iomem;

    uint32_t settings;

    uint32_t rdma_settings; /* access using atomics */
    uint32_t rdma_status;
    uint32_t rdma_sys_addr;
    uint32_t rdma_tbl_size;
    uint32_t rdma_tbl_size_internal;
    int rdma_active; /* access using atomics */

    uint32_t wdma_settings; /* access using atomics */
    uint32_t wdma_status;
    uint32_t wdma_sys_addr;
    uint32_t wdma_tbl_size;
    uint32_t wdma_tbl_size_internal;
    int wdma_active; /* access using atomics */

    /* virtsw */
    sw_controller *sw_ctrl;
    dma_addr_t rdma_len;
};

typedef struct RCMSpaceWireState RCMSpaceWireState;

#define TYPE_RCM_SPACEWIRE "rcm_spacewire"
#define RCM_SPACEWIRE(obj) OBJECT_CHECK(RCMSpaceWireState, (obj), TYPE_RCM_SPACEWIRE)

void rcm_sw_change_address_space(RCMSpaceWireState *s, AddressSpace *addr_space,
                                 Error **errp);

#endif
