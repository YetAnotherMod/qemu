#ifndef HW_MISC_COMM_H
#define HW_MISC_COMM_H

#include "hw/sysbus.h"
#include "chardev/char-fe.h"

struct CommState {
    SysBusDevice parent;
    MemoryRegion iomem;

    AddressSpace *addr_space;

    uint32_t comm_receive_main_counter;
    uint32_t comm_receive_address;
    uint32_t comm_receive_bias;
    uint32_t comm_receive_row_counter;
    uint32_t comm_receive_addressing_mode;
    uint32_t comm_receive_CSR;
    uint32_t comm_receive_interrupt_mask;
    uint32_t comm_receive_internal_state;

    uint32_t comm_transmit_main_counter;
    uint32_t comm_transmit_address;
    uint32_t comm_transmit_bias;
    uint32_t comm_transmit_row_counter;
    uint32_t comm_transmit_addressing_mode;
    uint32_t comm_transmit_CSR;
    uint32_t comm_transmit_interrupt_mask;
    uint32_t comm_transmit_internal_state;

    uint32_t comm_optional_hc;
    uint32_t comm_optional_err1_c;
    uint32_t comm_optional_err2_c;
    uint8_t registers_mode;

    CharBackend comm_chr;
    qemu_irq irq0;
    qemu_irq irq1;
};

typedef struct CommState CommState;
typedef uint32_t nmc_byte_t;

#define TYPE_COMM "comm"
#define COMM(obj) OBJECT_CHECK(CommState, (obj), TYPE_COMM)

void comm_change_address_space(CommState *s, AddressSpace *addr_space,
                               Error **errp);

#endif /* HW_MISC_COMM_H */
