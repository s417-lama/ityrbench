#!/bin/bash
[[ -z "${PS1+x}" ]] && set -euo pipefail

MPIEXEC=${MPIEXEC:-mpiexec}

$MPIEXEC --version

export MADM_RUN__=1
export MADM_PRINT_ENV=1
export PCAS_PRINT_ENV=1
export ITYR_PRINT_ENV=1

STDOUT_FILE=mpirun_out.txt

case $KOCHI_MACHINE in
  ito-a)
    ityr_mpirun() {
      local n_processes=$1
      local n_processes_per_node=$2

      if [[ $PJM_ENVIRONMENT == BATCH ]]; then
        OUTPUT_CMD="tee $STDOUT_FILE"
      else
        OUTPUT_CMD=cat
      fi
      $MPIEXEC -n $n_processes -N $n_processes_per_node \
        --mca prte_ssh_agent pjrsh \
        --hostfile $PJM_O_NODEINF \
        --mca osc_ucx_acc_single_intrinsic true \
        -- setarch $(uname -m) --addr-no-randomize "${@:3}" | $OUTPUT_CMD
    }
    ;;
  wisteria-o)
    export UTOFU_SWAP_PROTECT=1

    ityr_mpirun() {
      local n_processes=$1
      local n_processes_per_node=$2
      (
        if [[ $PJM_ENVIRONMENT == INTERACT ]]; then
          tee_cmd="tee $STDOUT_FILE"
          of_opt=""
        else
          export PLE_MPI_STD_EMPTYFILE=off # do not create empty stdout/err files
          tee_cmd="cat"
          of_opt="-of-proc $STDOUT_FILE"
          trap "compgen -G ${STDOUT_FILE}.* && tail -n +1 \$(ls ${STDOUT_FILE}.* -v) | tee $STDOUT_FILE && rm ${STDOUT_FILE}.*" EXIT
        fi
        vcoordfile=$(mktemp)
        trap "rm -f $vcoordfile" EXIT
        np=0
        if [[ -z ${PJM_NODE_Y+x} ]]; then
          # 1D
          for x in $(seq 1 $PJM_NODE_X); do
            for i in $(seq 1 $n_processes_per_node); do
              echo "($((x-1)))" >> $vcoordfile
              if (( ++np >= n_processes )); then
                break
              fi
            done
          done
        elif [[ -z ${PJM_NODE_Z+x} ]]; then
          # 2D
          for x in $(seq 1 $PJM_NODE_X); do
            for y in $(seq 1 $PJM_NODE_Y); do
              for i in $(seq 1 $n_processes_per_node); do
                echo "($((x-1)),$((y-1)))" >> $vcoordfile
                if (( ++np >= n_processes )); then
                  break 2
                fi
              done
            done
          done
        else
          # 3D
          for x in $(seq 1 $PJM_NODE_X); do
            for y in $(seq 1 $PJM_NODE_Y); do
              for z in $(seq 1 $PJM_NODE_Z); do
                for i in $(seq 1 $n_processes_per_node); do
                  echo "($((x-1)),$((y-1)),$((z-1)))" >> $vcoordfile
                  if (( ++np >= n_processes )); then
                    break 3
                  fi
                done
              done
            done
          done
        fi
        $MPIEXEC $of_opt -n $n_processes \
          --vcoordfile $vcoordfile \
          -- setarch $(uname -m) --addr-no-randomize "${@:3}" | $tee_cmd
      )
    }
    ;;
  *)
    ityr_mpirun() {
      local n_processes=$1
      local n_processes_per_node=$2
      $MPIEXEC -n $n_processes -N $n_processes_per_node \
        -- setarch $(uname -m) --addr-no-randomize "${@:3}"
    }
    ;;
esac

run_trace_viewer() {
  if [[ -z ${KOCHI_FORWARD_PORT+x} ]]; then
    echo "Trace viewer cannot be launched without 'kochi interact' command."
    exit 1
  fi
  shopt -s nullglob
  MLOG_VIEWER_ONESHOT=false bokeh serve $KOCHI_INSTALL_PREFIX_MASSIVELOGGER/viewer --port $KOCHI_FORWARD_PORT --allow-websocket-origin \* --args ityr_log_*.ignore pcas_log_*.ignore
}

export PCAS_ENABLE_SHARED_MEMORY=$KOCHI_PARAM_SHARED_MEM
export PCAS_MAX_DIRTY_CACHE_SIZE=$(bc <<< "$KOCHI_PARAM_MAX_DIRTY * 2^20 / 1")
export PCAS_PREFETCH_BLOCKS=0

export MADM_STACK_SIZE=$((4 * 1024 * 1024))

if [[ $KOCHI_PARAM_ALLOCATOR == jemalloc ]]; then
  export LD_PRELOAD=${KOCHI_INSTALL_PREFIX_JEMALLOC}/lib/libjemalloc.so${LD_PRELOAD:+:$LD_PRELOAD}
fi

if [[ $KOCHI_PARAM_DEBUGGER == 1 ]] && [[ -z "${PS1+x}" ]]; then
  echo "Use kochi interact to run debugger."
  exit 1
fi