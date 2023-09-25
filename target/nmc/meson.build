gen = [
  decodetree.process('insn.decode', extra_args: [ '--static-decode', 'decode_insn',
                                                  '--insnwidth', '32'])
]

nmc_ss = ss.source_set()
nmc_ss.add(gen)
nmc_ss.add(files(
  'translate.c'
))

target_arch += {'nmc': nmc_ss}

nmc_softmmu_ss = ss.source_set()

target_softmmu_arch += {'nmc': nmc_softmmu_ss}
