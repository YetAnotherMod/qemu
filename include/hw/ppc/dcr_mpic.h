#ifndef DCR_MPIC_H
#define DCR_MPIC_H

#define MAX_CPU_SUPPORTED	4

#define EXT_SOURCE_NUM		128
#define MAX_IPI_NUM			4
#define MAX_TIMER_NUM		4

typedef enum {
	OUTPUT_NON_CRIT,
	OUTPUT_CRIT,
	OUTPUT_MCHECK,

	OUTPUT_IRQ_NUM,
} output_type_t;

typedef struct {
	// Vector/Priority register data
	uint32_t vector : 8;
	uint32_t priority : 4;
	uint32_t sense : 1;
	uint32_t polarity : 1;
	uint32_t activity : 1;
	uint32_t masked : 1;
	// Destination register data
	uint32_t destination : 4;
	// Internal data
	uint32_t pending : 1;
} irq_config_t;

typedef struct {
	/* this are used in callbacks */
    void *state;
    uint32_t id;
    /* timer info */
    uint32_t count;
    uint32_t active;
    uint32_t toggle_bit;
} mpic_timer_t;

typedef struct {
	/* private */
	DeviceState parent_obj;

	/* properties */
	CPUState *cpu;
	uint32_t baseaddr;
	uint32_t timer_freq;

	/* public */
	bool pass_through_8259;

	irq_config_t irq[EXT_SOURCE_NUM + MAX_TIMER_NUM + MAX_IPI_NUM];

	uint32_t task_prio[MAX_CPU_SUPPORTED];

	uint32_t vitc_crit_border;
	uint32_t vitc_mcheck_border;

	// spurious vector
	uint32_t spv;

	GList *current_irq_list[MAX_CPU_SUPPORTED][OUTPUT_IRQ_NUM];
	irq_config_t *pending_irqs[MAX_CPU_SUPPORTED][OUTPUT_IRQ_NUM];

	uint32_t freq_reg;
	QEMUTimer qemu_timer[MAX_TIMER_NUM];
	mpic_timer_t timer_data[MAX_TIMER_NUM];

	QemuMutex mutex;

	qemu_irq output_irq[OUTPUT_IRQ_NUM];
} MpicState;

#define TYPE_MPIC "dcr-mpic"
#define MPIC(obj) \
    OBJECT_CHECK(MpicState, obj, TYPE_MPIC)

#endif
