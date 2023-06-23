// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Re-export types from crosvm_base that are used in CrOS.
// Note: This list is supposed to shrink over time as crosvm_base functionality is replaced with
// third_party crates or ChromeOS specific implementations.
// Do not add to this list.
pub use crosvm_base::add_fd_flags;
pub use crosvm_base::block_signal;
pub use crosvm_base::debug;
pub use crosvm_base::errno_result;
pub use crosvm_base::error;
pub use crosvm_base::getpid;
pub use crosvm_base::gettid;
pub use crosvm_base::handle_eintr;
pub use crosvm_base::handle_eintr_errno;
pub use crosvm_base::info;
pub use crosvm_base::ioctl;
pub use crosvm_base::ioctl_io_nr;
pub use crosvm_base::ioctl_ior_nr;
pub use crosvm_base::ioctl_iow_nr;
pub use crosvm_base::ioctl_iowr_nr;
pub use crosvm_base::ioctl_with_mut_ptr;
pub use crosvm_base::ioctl_with_ptr;
pub use crosvm_base::ioctl_with_val;
pub use crosvm_base::net;
pub use crosvm_base::pipe;
pub use crosvm_base::syscall;
pub use crosvm_base::syslog;
pub use crosvm_base::unblock_signal;
pub use crosvm_base::warn;
pub use crosvm_base::AsRawDescriptor;
pub use crosvm_base::Error;
pub use crosvm_base::FromRawDescriptor;
pub use crosvm_base::IntoRawDescriptor;
pub use crosvm_base::MappedRegion;
pub use crosvm_base::MemoryMapping;
pub use crosvm_base::MemoryMappingBuilder;
pub use crosvm_base::RawDescriptor;
pub use crosvm_base::Result;
pub use crosvm_base::SafeDescriptor;
pub use crosvm_base::ScmSocket;
pub use crosvm_base::Terminal;

// TODO(b/287484575): Flatten the namespace
pub mod signal {
    pub use crosvm_base::signal::Error;
}
pub mod unix {
    pub use crosvm_base::unix::clear_signal_handler;
    pub use crosvm_base::unix::duration_to_timespec;
    pub use crosvm_base::unix::errno_result;
    pub use crosvm_base::unix::getpid;
    pub use crosvm_base::unix::getsid;
    pub use crosvm_base::unix::gettid;
    pub use crosvm_base::unix::ioctl;
    pub use crosvm_base::unix::net;
    pub use crosvm_base::unix::pagesize;
    pub use crosvm_base::unix::panic_handler;
    pub use crosvm_base::unix::pipe;
    pub use crosvm_base::unix::reap_child;
    pub use crosvm_base::unix::register_signal_handler;
    pub use crosvm_base::unix::round_up_to_page_size;
    pub use crosvm_base::unix::set_rt_prio_limit;
    pub use crosvm_base::unix::set_rt_round_robin;
    pub use crosvm_base::unix::setsid;
    pub use crosvm_base::unix::signal::Error;
    pub use crosvm_base::unix::vsock;
    pub use crosvm_base::unix::wait_for_interrupt;
    pub use crosvm_base::unix::KillOnDrop;
    pub use crosvm_base::unix::MemfdSeals;
    pub use crosvm_base::unix::Pid;
    pub use crosvm_base::unix::ScmSocket;
    pub use crosvm_base::unix::SharedMemory;
    pub mod signal {
        pub use crosvm_base::unix::signal::Error;
    }
}