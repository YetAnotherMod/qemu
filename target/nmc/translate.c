#include "qemu/osdep.h"
#include "tcg/tcg.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "tcg/tcg-op.h"
#include "exec/translator.h"

static TCGv cpu_pc;

typedef struct DisasContext DisasContext;

struct DisasContext {
    DisasContextBase base;

    CPUNMCState *env;
};

static void gen_goto_tb(DisasContext *ctx, int n, target_ulong dest)
{
    const TranslationBlock *tb = ctx->base.tb;

    if (translator_use_goto_tb(&ctx->base, dest)) {
        tcg_gen_goto_tb(n);
        tcg_gen_movi_i32(cpu_pc, dest);
        tcg_gen_exit_tb(tb, n);
    } else {
        tcg_gen_movi_i32(cpu_pc, dest);
        tcg_gen_lookup_and_goto_ptr();
    }
    ctx->base.is_jmp = DISAS_NORETURN;
}

#include "decode-insn.c.inc"

static bool trans_NOP(DisasContext *ctx, arg_NOP *a)
{
    return true;
}

static void nmc_tr_init_disas_context(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    CPUNMCState *env = cs->env_ptr;

    ctx->env = env;
}

static void nmc_tr_tb_start(DisasContextBase *db, CPUState *cs)
{}

static void nmc_tr_insn_start(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    tcg_gen_insn_start(ctx->base.pc_first);
}

static void nmc_tr_translate_insn(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    unsigned int insn = cpu_lduw_code(ctx->env, ctx->base.pc_next);
    decode_insn(ctx, insn);

    ctx->base.pc_next += 4;
}

static void nmc_tr_tb_stop(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    switch (ctx->base.is_jmp) {
    case DISAS_NEXT:
    case DISAS_TOO_MANY:
        gen_goto_tb(ctx, 1, ctx->base.pc_next);
        break;
    
    default:
        break;
    }
}

static const TranslatorOps nmc_tr_ops = {
    .init_disas_context = nmc_tr_init_disas_context,
    .tb_start           = nmc_tr_tb_start,
    .insn_start         = nmc_tr_insn_start,
    .translate_insn     = nmc_tr_translate_insn,
    .tb_stop            = nmc_tr_tb_stop,
};

void gen_intermediate_code(CPUState *cs, TranslationBlock *tb, int max_insns)
{
    DisasContext ctx = { };
    translator_loop(&nmc_tr_ops, &ctx.base, cs, tb, max_insns);
}

void restore_state_to_opc(CPUNMCState *env, TranslationBlock *tb,
                            target_ulong *data)
{
    env->pc = data[0];
}
