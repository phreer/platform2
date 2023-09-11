#!/bin/bash

set -e

create_vfs()
{
    local SYSFS_I915_DIR="/sys/bus/pci/devices/0000:00:02.0"
    local vendor=$(cat "$SYSFS_I915_DIR"/vendor)
    if [[ "$vendor" != "0x8086" ]]; then
        return;
    fi
    local device_id=$(cat "$SYSFS_I915_DIR"/device)
    local num_vfs=$1
    # There are two GTs on MeteorLake.
    if [[ "$device_id" == "0x7d45" ]]; then
        local num_gts=2
    else
        local num_gts=1
    fi
    if [[ "$num_vfs" < 0 ]] || [[ "$num_vfs" > 7 ]]; then
        echo "Invalid number of VFs (expected range: 0 to 7), got $num_vfs"
        exit -1
    fi
    # Currently these values are chosen empirically so that GPU won't hang.
    # From our experience, these values have no impact to host graphics
    # performance.
    local pf_exec_quantum_ms=40
    local pf_preempt_timeout_us=40000
    local vf_exec_quantum_ms=40
    local vf_preempt_timeout_us=40000
    echo "$num_vfs" > "${SYSFS_I915_DIR}/sriov_numvfs"
    echo "$pf_exec_quantum_ms" > "${SYSFS_I915_DIR}/drm/card0/prelim_iov/pf/gt0/exec_quantum_ms"
    echo "$pf_preempt_timeout_us" > "${SYSFS_I915_DIR}/drm/card0/prelim_iov/pf/gt0/preempt_timeout_us"
    for i_vf in $(seq 1 $num_vfs); do
        for i_gt in $(seq 0 $(( $num_gts - 1 )) ); do
            echo "$vf_exec_quantum_ms" > "${SYSFS_I915_DIR}/drm/card0/prelim_iov/vf${i_vf}/gt${i_gt}/exec_quantum_ms"
            echo "$vf_preempt_timeout_us" > "${SYSFS_I915_DIR}/drm/card0/prelim_iov/vf${i_vf}/gt${i_gt}/preempt_timeout_us"
        done
    done
}

help_manual()
{
    echo "Usage: $0 -m|-c NUM_VFS|-r"
    echo "-c NUM_VFS: create SR-IOV VFs."
    echo "-r        : remove all SR-IOV VFs."
    echo "-m        : Show this help manual."
}

main()
{
    while getopts ":mc:r" opt; do
        case $opt in
            m)
                help_manual
                exit 0
                ;;
            c)
                create_vfs "$OPTARG"
                exit 0
                ;;
            r)
                create_vfs 0
                exit 0
                ;;
        esac
    done
}

main "$@"
