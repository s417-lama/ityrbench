#!/bin/bash
# set -euo pipefail

MPICXX=${MPICXX:-mpicxx}

mpirun --version || true
$MPICXX --version

export MADM_RUN__=1
export MADM_PRINT_ENV=1
export PCAS_PRINT_ENV=1
export ITYR_PRINT_ENV=1

case $KOCHI_MACHINE in
  ito-a)
    cores=36
    nodes=$PJM_VNODES

    ityr_mpirun() {
      local n_processes=$1
      local n_processes_per_node=$2
      mpirun -n $n_processes -N $n_processes_per_node \
        --mca plm_rsh_agent pjrsh \
        --hostfile $PJM_O_NODEINF \
        --mca osc_ucx_acc_single_intrinsic true \
        $KOCHI_INSTALL_PREFIX_MASSIVETHREADS_DM/bin/madm_disable_aslr "${@:3}"
    }
    ;;
  wisteria-o)
    cores=48
    nodes=$PJM_NODE

    ityr_mpirun() {
      local n_processes=$1
      local n_processes_per_node=$2
      STDOUT_FILE=mpirun_out.txt
      if [[ $PJM_ENVIRONMENT == INTERACT ]]; then
        of_opt=""
      else
        of_opt="-of $STDOUT_FILE"
      fi
      mpirun $of_opt -n $n_processes \
        --vcoordfile <(
          np=0
          if [[ -z ${PJM_NODE_Y+x} ]]; then
            # 1D
            for x in $(seq 1 $PJM_NODE_X); do
              for i in $(seq 1 $n_processes_per_node); do
                echo "($((x-1)))"
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
                  echo "($((x-1)),$((y-1)))"
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
                    echo "($((x-1)),$((y-1)),$((z-1)))"
                    if (( ++np >= n_processes )); then
                      break 3
                    fi
                  done
                done
              done
            done
          fi
        ) \
        $KOCHI_INSTALL_PREFIX_MASSIVETHREADS_DM/bin/madm_disable_aslr "${@:3}" | tee $STDOUT_FILE
      if [[ $PJM_ENVIRONMENT == BATCH ]]; then
        cat $STDOUT_FILE
      fi
    }
    ;;
  *)
    cores=6
    nodes=1

    ityr_mpirun() {
      local n_processes=$1
      local n_processes_per_node=$2
      mpirun -n $n_processes -N $n_processes_per_node \
        $KOCHI_INSTALL_PREFIX_MASSIVETHREADS_DM/bin/madm_disable_aslr "${@:3}"
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
