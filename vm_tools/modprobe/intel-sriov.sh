#!/bin/bash

set -e

create_vfs()
{
    local num_vfs=$1
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
    echo "$num_vfs" > "/sys/bus/pci/devices/0000:00:02.0/sriov_numvfs"
    echo "$pf_exec_quantum_ms" > "/sys/bus/pci/devices/0000:00:02.0/drm/card0/prelim_iov/pf/gt0/exec_quantum_ms"
    echo "$pf_preempt_timeout_us" > "/sys/bus/pci/devices/0000:00:02.0/drm/card0/prelim_iov/pf/gt0/preempt_timeout_us"
    for i_vf in $(seq 1 $num_vfs); do
        echo "$vf_exec_quantum_ms" > "/sys/bus/pci/devices/0000:00:02.0/drm/card0/prelim_iov/vf$i_vf/gt0/exec_quantum_ms"
        echo "$vf_preempt_timeout_us" > "/sys/bus/pci/devices/0000:00:02.0/drm/card0/prelim_iov/vf$i_vf/gt0/preempt_timeout_us"
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
	        create_vfs $OPTARG
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
