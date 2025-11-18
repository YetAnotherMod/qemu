#ifndef HW_DMA_PLB6
#define HW_DMA_PLB6

#define NUMBER_OF_CHANNELS 4
#define NUMBER_OF_IRQS NUMBER_OF_CHANNELS

typedef union ctrl_reg_s {
    struct {
        uint32_t : 12;  // reserved
        uint32_t : 1;   // destination address snoopable
        uint32_t : 1;   // source address snoopable
        uint32_t : 3;   // transfer width
        uint32_t : 2;   // channel priority
        uint32_t terminal_count : 1;
        uint32_t : 1;  // data striding on the dest
        uint32_t : 1;  // data striding on the source
        uint32_t : 2;  // reserved
        uint32_t transfer_width : 3;
        uint32_t interrupt_mode : 2;
        uint32_t error_interrupt : 1;
        uint32_t terminal_count_interrupt : 1;
        uint32_t channel_enable : 1;
    };
    uint32_t reg_value;
} ctrl_reg_s;

typedef union status_reg_s {
    struct {
        uint32_t : 4;  // reserved
        uint32_t : 4;  // scatter-gather status
        uint32_t : 4;  // chanel_busy
        uint32_t : 3;  // channel4_error_spec
        uint32_t channel3_error_enable : 1;
        uint32_t : 3;  // channel3_error_spec
        uint32_t channel2_error_enable : 1;
        uint32_t : 3;  // channel2_error_spec
        uint32_t channel1_error_enable : 1;
        uint32_t : 3;  // channel1_error_spec
        uint32_t channel0_error_enable : 1;
        uint32_t channel_terminal_count_status : 4;
    };
    uint32_t reg_value;
} status_reg_s;

typedef struct channel {
    ctrl_reg_s ctrl_reg;
    uint32_t count_reg;
    uint64_t src_addr_reg;
    uint64_t dst_addr_reg;
} plb6_dma_channel;

typedef struct PLB6DMAState {
    DeviceState parent_obj;

    CPUState *cpu;
    uint32_t baseaddr;

    plb6_dma_channel channels[NUMBER_OF_CHANNELS];
    qemu_irq irqs[NUMBER_OF_IRQS];
    status_reg_s status_reg;
    uint32_t arbiter_mode;
} PLB6DMAState;

#define TYPE_PLB6_DMA "PLB6_DMA"
#define PLB6_DMA(obj) OBJECT_CHECK(PLB6DMAState, (obj), TYPE_PLB6_DMA)

#endif /*HW_DMA_PLB6*/
