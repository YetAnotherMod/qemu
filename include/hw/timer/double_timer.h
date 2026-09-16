/**
 * @file double_timer.h
 * @author Нелюбин Виктор. ЗАО НТЦ Модуль (v.nelyubin@module.ru)
 * @brief контроллер DIT (сдвоенный интервальный таймер)
 * @version 0.1
 * @date 2026-09-07
 * 
 * 
 */

#ifndef HW_TIMER_DOUBLE_TIMER_H
#define HW_TIMER_DOUBLE_TIMER_H

#include "qemu/timer.h"
#include "hw/ptimer.h"

#define TYPE_DOUBLE_TIMER "double-timer"
OBJECT_DECLARE_SIMPLE_TYPE(DoubleTimerState, DOUBLE_TIMER)
    
/// опорная частота модуля DIT
#define DOUBLE_TIMER_MAIN_FREQ "dcr-freq-hz"
/// имя линии прерывания таймера
#define DOUBLE_TIMER_INT_NAME   "dit-int"
    
/// размер регистрового файла контроллера
#define DOUBLE_TIMER_REG_SZB 0x1000U
/// размер блока регистров таймера
#define TIMER_REG_BLOCK_SZB 0x20U

/*
 * Number of timers in the device.
 */
#define NUM_TIMERS 2U

/*
 * Timer register offsets (each 32-bit, 4-byte aligned).
 * Timer 1 registers start at offset 0x00, Timer 2 at TIMER_REG_BLOCK_SZ.
 */

/* TimerXLoad */
#define TIMER_LOAD_OFFSET(t) ((t)*TIMER_REG_BLOCK_SZB + 0x00U)
/* TimerXValue */
#define TIMER_VALUE_OFFSET(t) ((t)*TIMER_REG_BLOCK_SZB + 0x04U)
/* TimerXControl */
#define TIMER_CONTROL_OFFSET(t) ((t)*TIMER_REG_BLOCK_SZB + 0x08U)
/* TimerXIntClr */
#define TIMER_INT_CLR_OFFSET(t) ((t)*TIMER_REG_BLOCK_SZB + 0x0CU)
/* TimerXRIS */
#define TIMER_RIS_OFFSET(t) ((t)*TIMER_REG_BLOCK_SZB + 0x10U)
/* TimerXMIS */
#define TIMER_MIS_OFFSET(t) ((t)*TIMER_REG_BLOCK_SZB + 0x14U)
/* TimerXBGLoad */
#define TIMER_BG_LOAD_OFFSET(t) ((t)*TIMER_REG_BLOCK_SZB + 0x18U)

/*
 * Peripheral ID registers (shared, after timer registers).
 * 0x00041804
 */
#define PERIPH_ID_0_OFFSET 0xFE0U /* 0x04 */
#define PERIPH_ID_1_OFFSET 0xFE4U /* 0x18 */
#define PERIPH_ID_2_OFFSET 0xFE8U /* 0x04 */
#define PERIPH_ID_3_OFFSET 0xFECU /* 0x00 */

/// значения регистров
#define PERIPH_ID_0_VAL 0x04U
#define PERIPH_ID_1_VAL 0x18U
#define PERIPH_ID_2_VAL 0x04U
#define PERIPH_ID_3_VAL 0x00U

/*
 * PCell ID registers.
 */
#define PCELL_ID_0_OFFSET 0xFF0U /* 0x0D */
#define PCELL_ID_1_OFFSET 0xFF4U /* 0xF0 */
#define PCELL_ID_2_OFFSET 0xFF8U /* 0x05 */
#define PCELL_ID_3_OFFSET 0xFFCU /* 0xB1 */
    
/// значения регистров
#define PCELL_ID_0_VAL 0x0DU
#define PCELL_ID_1_VAL 0xF0U 
#define PCELL_ID_2_VAL 0x05U 
#define PCELL_ID_3_VAL 0xB1U 

/*
 * TimerXControl register bit definitions.
 */
#define CONTROL_TIMER_EN (1U << 7)   /* Timer enable */
#define CONTROL_TIMER_MODE_LOAD (1U << 6) /* 0=max, 1=load */
#define CONTROL_INT_ENABLE (1U << 5) /* Interrupt enable */
#define CONTROL_PRESCALE_SHIFT 2U    /* Bits 3:2 */
#define CONTROL_PRESCALE_MASK 0x3U
#define CONTROL_TIMER_SIZE_32 (1U << 1) /* 0=16-bit, 1=32-bit */
#define CONTROL_ONE_SHOT (1U << 0)   /* 0=periodic, 1=one-shot */

/// timerXControl значение по умолчанию
/// @warning  странно - прерывание включено по умолчанию
#define CONTROL_VAL_DEFAULT CONTROL_INT_ENABLE

/*
 * Prescaler values.
 */
#define PRESCALE_1 1U
#define PRESCALE_16 16U
#define PRESCALE_256 256U

/*
 * Maximum counter values.
 */
#define COUNTER_MAX_16 0xFFFFU
#define COUNTER_MAX_32 0xFFFFFFFFU

/*
 * Per-timer state structure.
 */
typedef struct TimerUnitState 
{
    ptimer_state *ptimer; /* ptimer handle */
    qemu_irq irq;         /* interrupt line */

    uint32_t load;    /* TimerXLoad */
    uint32_t bg_load; /* TimerXBGLoad (pending load) */
    uint32_t control; /* TimerXControl */
    uint32_t ris;     /* TimerXRIS (raw interrupt status) */
    uint32_t mis;     /* TimerXMIS (masked interrupt status) */

    bool bg_load_pending; /* flag: bg_load should be applied on next zero */
    uint64_t freq_hz;     /* timer operating frequency (after prescaler) */
} TimerUnitState;

/*
 * DIT device state.
 */
struct DoubleTimerState 
{
    DeviceState parent_obj;

	/* properties */
	CPUState *cpu; /*link to cpu*/
    uint32_t base_freq_hz; /* base frequency (property) */
	uint32_t baseaddr;/*base address on dcr bus*/

    TimerUnitState timers[NUM_TIMERS];
};

#endif /* HW_TIMER_DOUBLE_TIMER_H */