#!/bin/bash
set -euo pipefail

MPICXX=${MPICXX:-mpicxx}

mpirun --version || true
$MPICXX --version

CFLAGS=${CFLAGS:-""}
CFLAGS="$CFLAGS -DNDEBUG"

MPICXX=$MPICXX CFLAGS=$CFLAGS make cilksort.out

export MADM_RUN__=1
export MADM_PRINT_ENV=1

n=1
# n=6
# n=36
# n=48

if [[ $KOCHI_MACHINE == ito-a ]]; then
  mpirun -n $n \
    --mca plm_rsh_agent pjrsh \
    --hostfile $PJM_O_NODEINF \
    --mca osc_ucx_acc_single_intrinsic true \
    --bind-to core \
    $KOCHI_INSTALL_PREFIX_MASSIVETHREADS_DM/bin/madm_disable_aslr ./cilksort.out $@
else
  mpirun -n $n $KOCHI_INSTALL_PREFIX_MASSIVETHREADS_DM/bin/madm_disable_aslr ./cilksort.out $@
fi
