#ifndef QEMU_NMC_CPU_H
#define QEMU_NMC_CPU_H

#include "cpu-qom.h"
#include "exec/cpu-defs.h"

#define NMC_CPU_TYPE_SUFFIX "-" TYPE_NMC_CPU
#define NMC_CPU_TYPE_NAME(name) (name NMC_CPU_TYPE_SUFFIX)
#define CPU_RESOLVING_TYPE TYPE_NMC_CPU

#define TCG_GUEST_DEFAULT_MO 0

typedef struct CPUNMCState CPUNMCState;

struct CPUNMCState {
    uint32_t pc;
};

typedef struct NMCCPU {
    /*< private >*/
    CPUState parent_obj;
    /*< public >*/

    CPUNegativeOffsetState neg;

    CPUNMCState env;
} NMCCPU;

static inline void cpu_get_tb_cpu_state(CPUNMCState *env, target_ulong *pc,
                                        target_ulong *cs_base, uint32_t *pflags)
{
    *pc = env->pc;
    *cs_base = 0;
    *pflags = 0;
}

static inline int cpu_mmu_index(CPUNMCState *env, bool ifetch)
{
    return 0;
}

typedef CPUNMCState CPUArchState;
typedef NMCCPU ArchCPU;

#include "exec/cpu-all.h"

#endif
