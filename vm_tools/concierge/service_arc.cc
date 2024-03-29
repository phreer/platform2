// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <stdlib.h>

#include <string>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <vboot/crossystem.h>
#include <libcrossystem/crossystem.h>

#include "vm_tools/common/naming.h"
#include "vm_tools/common/pstore.h"
#include "vm_tools/concierge/arc_vm.h"
#include "vm_tools/concierge/service.h"
#include "vm_tools/concierge/shared_data.h"
#include "vm_tools/concierge/vm_util.h"

namespace vm_tools {
namespace concierge {

namespace {

// Android data directory.
constexpr char kAndroidDataDir[] = "/run/arcvm/android-data";

// Android stub volume directory for MyFiles and removable media.
constexpr char kStubVolumeSharedDir[] = "/run/arcvm/media";

// Path to the VM guest kernel.
constexpr char kKernelPath[] = "/opt/google/vms/android/vmlinux";

// Path to the VM rootfs image file.
constexpr char kRootfsPath[] = "/opt/google/vms/android/system.raw.img";

// Path to the VM fstab file.
constexpr char kFstabPath[] = "/run/arcvm/host_generated/fstab";

constexpr char kArcVmName[] = "arcvm";
// /home/root/<hash>/crosvm is bind-mounted to /run/daemon-store/crosvm on
// sign-in.
constexpr char kCryptohomeRoot[] = "/run/daemon-store/crosvm";
constexpr char kPstoreExtension[] = ".pstore";

// Returns |image_path| on production. Returns a canonicalized path of the image
// file when in dev mode.
base::FilePath GetImagePath(const base::FilePath& image_path,
                            bool is_dev_mode) {
  if (!is_dev_mode)
    return image_path;

  // When in dev mode, the Android images might be on the stateful partition and
  // |kRootfsPath| might be a symlink to the stateful partition image file. In
  // that case, we need to use the resolved path so that brillo::SafeFD calls
  // can handle the path without errors. The same is true for vendor.raw.image
  // too. On the other hand, when in production mode, we should NEVER do the
  // special handling. In production, the images files in /opt should NEVER ever
  // be a symlink.

  // We cannot use base::NormalizeFilePath because the function fails
  // if |path| points to a directory (for Windows compatibility.)
  char buf[PATH_MAX] = {};
  if (realpath(image_path.value().c_str(), buf))
    return base::FilePath(buf);
  if (errno != ENOENT)
    PLOG(WARNING) << "Failed to resolve " << image_path.value();
  return image_path;
}

base::FilePath GetPstoreDest(const std::string& owner_id) {
  return base::FilePath(kCryptohomeRoot)
      .Append(owner_id)
      .Append(vm_tools::GetEncodedName(kArcVmName))
      .AddExtension(kPstoreExtension);
}

// Returns true if the path is a valid demo image path.
bool IsValidDemoImagePath(const base::FilePath& path) {
  // A valid demo image path looks like:
  //   /run/imageloader/demo-mode-resources/<version>/android_demo_apps.squash
  //   <version> part looks like 0.12.34.56 ("[0-9]+(.[0-9]+){0,3}" in regex).
  std::vector<std::string> components = path.GetComponents();
  return components.size() == 6 && components[0] == "/" &&
         components[1] == "run" && components[2] == "imageloader" &&
         components[3] == "demo-mode-resources" &&
         base::ContainsOnlyChars(components[4], "0123456789.") &&
         !base::StartsWith(components[4], ".") &&
         components[5] == "android_demo_apps.squash";
}

// Returns true if the path is a valid data image path.
bool IsValidDataImagePath(const base::FilePath& path) {
  std::vector<std::string> components = path.GetComponents();
  // A disk image created by concierge:
  // /run/daemon-store/crosvm/<hash>/YXJjdm0=.img
  if (components.size() == 6 && components[0] == "/" &&
      components[1] == "run" && components[2] == "daemon-store" &&
      components[3] == "crosvm" &&
      base::ContainsOnlyChars(components[4], "0123456789abcdef") &&
      components[5] == vm_tools::GetEncodedName(kArcVmName) + ".img")
    return true;
  // An LVM block device:
  // /dev/mapper/vm/dmcrypt-<hash>-arcvm
  if (components.size() == 5 && components[0] == "/" &&
      components[1] == "dev" && components[2] == "mapper" &&
      components[3] == "vm" && base::StartsWith(components[4], "dmcrypt-") &&
      base::EndsWith(components[4], "-arcvm"))
    return true;
  return false;
}

// TODO(hashimoto): Move VM configuration logic from chrome to concierge and
// remove this function. b/219677829
// Returns true if the StartArcVmRequest contains valid ARCVM config values.
bool ValidateStartArcVmRequest(StartArcVmRequest* request) {
  // List of allowed kernel parameters.
  const std::set<std::string> kAllowedKernelParams = {
      "androidboot.arc_generate_pai=1",
      "androidboot.arcvm_mount_debugfs=1",
      "androidboot.disable_download_provider=1",
      "androidboot.disable_media_store_maintenance=1",
      "androidboot.arc.tts.caching=1",
      "androidboot.arc_enable_gmscore_lmk_protection=1",
      "androidboot.arc.broadcast_anr_prenotify=1",
      "rw",
  };

  // List of allowed kernel parameter prefixes.
  const std::vector<std::string> kAllowedKernelParamPrefixes = {
      "androidboot.arc_custom_tabs=",
      "androidboot.arc_dalvik_memory_profile=",
      "androidboot.arc_file_picker=",
      "androidboot.arcvm.logd.size=",
      "androidboot.arcvm_metrics_mem_psi_period=",
      "androidboot.arcvm_ureadahead_mode=",
      "androidboot.arcvm_virtio_blk_data=",
      "androidboot.enable_notifications_refresh=",
      "androidboot.host_is_in_vm=",
      "androidboot.keyboard_shortcut_helper_integration=",
      "androidboot.lcd_density=",
      "androidboot.native_bridge=",
      "androidboot.play_store_auto_update=",
      "androidboot.usap_profile=",
      "androidboot.zram_size=",
      // TODO(hashimoto): This param was removed in R98. Remove this.
      "androidboot.image_copy_paste_compat=",
  };
  // Filter kernel params.
  const std::vector<std::string> params(request->params().begin(),
                                        request->params().end());
  request->clear_params();
  for (const auto& param : params) {
    if (kAllowedKernelParams.count(param) != 0) {
      request->add_params(param);
      continue;
    }

    auto it = std::find_if(kAllowedKernelParamPrefixes.begin(),
                           kAllowedKernelParamPrefixes.end(),
                           [&param](const std::string& prefix) {
                             return base::StartsWith(param, prefix);
                           });
    if (it != kAllowedKernelParamPrefixes.end()) {
      request->add_params(param);
      continue;
    }

    LOG(WARNING) << param << " was removed because it doesn't match with any "
                 << "allowed param or prefix";
  }

  // Validate disks.
  constexpr char kEmptyDiskPath[] = "/dev/null";
  if (request->disks().size() < 1 || request->disks().size() > 4) {
    LOG(ERROR) << "Invalid number of disks: " << request->disks().size();
    return false;
  }
  // Disk #0 must be /opt/google/vms/android/vendor.raw.img.
  if (request->disks()[0].path() != "/opt/google/vms/android/vendor.raw.img") {
    LOG(ERROR) << "Disk #0 has invalid path: " << request->disks()[0].path();
    return false;
  }
  // Disk #1 must be a valid demo image path or /dev/null.
  if (request->disks().size() >= 2 &&
      !IsValidDemoImagePath(base::FilePath(request->disks()[1].path())) &&
      request->disks()[1].path() != kEmptyDiskPath) {
    LOG(ERROR) << "Disk #1 has invalid path: " << request->disks()[1].path();
    return false;
  }
  // Disk #2 must be /opt/google/vms/android/apex/payload.img or /dev/null.
  if (request->disks().size() >= 3 &&
      request->disks()[2].path() !=
          "/opt/google/vms/android/apex/payload.img" &&
      request->disks()[2].path() != kEmptyDiskPath) {
    LOG(ERROR) << "Disk #2 has invalid path: " << request->disks()[2].path();
    return false;
  }
  // Disk #3 must be a valid data image path or /dev/null.
  if (request->disks().size() >= kDataDiskIndex + 1) {
    const std::string& disk_path = request->disks()[kDataDiskIndex].path();
    if (!IsValidDataImagePath(base::FilePath(disk_path)) &&
        disk_path != kEmptyDiskPath) {
      LOG(ERROR) << "Disk #3 has invalid path: " << disk_path;
      return false;
    }
    LOG(INFO) << "Android /data disk path: " << disk_path;
  }
  return true;
}

}  // namespace

StartVmResponse Service::StartArcVm(StartArcVmRequest request,
                                    std::unique_ptr<dbus::MessageReader> reader,
                                    VmMemoryId vm_memory_id) {
  LOG(INFO) << "Received StartArcVm request";
  StartVmResponse response;
  response.set_status(VM_STATUS_FAILURE);

  VmInfo* vm_info = response.mutable_vm_info();
  vm_info->set_vm_type(VmInfo::ARC_VM);

  if (request.disks_size() > kMaxExtraDisks) {
    LOG(ERROR) << "Rejecting request with " << request.disks_size()
               << " extra disks";

    response.set_failure_reason("Too many extra disks");
    return response;
  }

  // TODO(hashimoto): Move VM configuration logic from chrome to concierge and
  // remove this check. b/219677829
  if (!ValidateStartArcVmRequest(&request)) {
    response.set_failure_reason("Invalid request");
    return response;
  }

  std::vector<Disk> disks;
  // Exists just to keep FDs around for crosvm to inherit
  std::vector<brillo::SafeFD> owned_fds;
  auto root_fd = brillo::SafeFD::Root();

  if (brillo::SafeFD::IsError(root_fd.second)) {
    LOG(ERROR) << "Could not open root directory: "
               << static_cast<int>(root_fd.second);
    response.set_failure_reason("Could not open root directory");
    return response;
  }

  // The rootfs can be treated as a disk as well and needs to be added before
  // other disks.
  Disk::Config rootfs_config{};
  rootfs_config.o_direct = false;
  rootfs_config.writable = request.rootfs_writable();
  const size_t rootfs_block_size = request.rootfs_block_size();
  if (rootfs_block_size) {
    rootfs_config.block_size = rootfs_block_size;
  }
  const bool is_dev_mode = (VbGetSystemPropertyInt("cros_debug") == 1);
  auto rootfsPath = GetImagePath(base::FilePath(kRootfsPath), is_dev_mode);
  auto failure_reason = ConvertToFdBasedPath(
      root_fd.first, &rootfsPath, rootfs_config.writable ? O_RDWR : O_RDONLY,
      owned_fds);
  if (!failure_reason.empty()) {
    LOG(ERROR) << "Could not open rootfs image" << rootfsPath;
    response.set_failure_reason("Rootfs path does not exist");
    return response;
  }

  disks.push_back(Disk(rootfsPath, rootfs_config));

  for (const auto& disk : request.disks()) {
    Disk::Config config{};
    base::FilePath path =
        GetImagePath(base::FilePath(disk.path()), is_dev_mode);
    if (!base::PathExists(path)) {
      LOG(ERROR) << "Missing disk path: " << path;
      response.set_failure_reason("One or more disk paths do not exist");
      return response;
    }
    config.writable = disk.writable();
    const size_t block_size = disk.block_size();
    if (block_size) {
      config.block_size = block_size;
    }
    failure_reason = ConvertToFdBasedPath(
        root_fd.first, &path, config.writable ? O_RDWR : O_RDONLY, owned_fds);

    if (!failure_reason.empty()) {
      LOG(ERROR) << "Could not open disk file";
      response.set_failure_reason(failure_reason);
      return response;
    }

    disks.push_back(Disk(path, config));
  }

  base::FilePath data_disk_path;
  if (request.disks().size() > kDataDiskIndex) {
    const std::string disk_path = request.disks()[kDataDiskIndex].path();
    if (IsValidDataImagePath(base::FilePath(disk_path)))
      data_disk_path = base::FilePath(disk_path);
  }

  // Create the runtime directory.
  base::FilePath runtime_dir;
  if (!base::CreateTemporaryDirInDir(base::FilePath(kRuntimeDir), "vm.",
                                     &runtime_dir)) {
    PLOG(ERROR) << "Unable to create runtime directory for VM";

    response.set_failure_reason(
        "Internal error: unable to create runtime directory");
    return response;
  }

  // Allocate resources for the VM.
  uint32_t vsock_cid = vsock_cid_pool_.Allocate();
  if (vsock_cid == 0) {
    LOG(ERROR) << "Unable to allocate vsock context id";

    response.set_failure_reason("Unable to allocate vsock cid");
    return response;
  }
  vm_info->set_cid(vsock_cid);

  std::unique_ptr<patchpanel::Client> network_client =
      patchpanel::Client::New(bus_);
  if (!network_client) {
    LOG(ERROR) << "Unable to open networking service client";

    response.set_failure_reason("Unable to open network service client");
    return response;
  }

  // Map the chronos user (1000) and the chronos-access group (1001) to the
  // AID_EXTERNAL_STORAGE user and group (1077).
  uint32_t seneschal_server_port = next_seneschal_server_port_++;
  std::unique_ptr<SeneschalServerProxy> server_proxy =
      SeneschalServerProxy::CreateVsockProxy(bus_, seneschal_service_proxy_,
                                             seneschal_server_port, vsock_cid,
                                             {{1000, 1077}}, {{1001, 1077}});
  if (!server_proxy) {
    LOG(ERROR) << "Unable to start shared directory server";

    response.set_failure_reason("Unable to start shared directory server");
    return response;
  }

  uint32_t seneschal_server_handle = server_proxy->handle();
  vm_info->set_seneschal_server_handle(seneschal_server_handle);

  crossystem::CrossystemImpl cros_system;
  std::vector<std::string> params =
      ArcVm::GetKernelParams(&cros_system, seneschal_server_port, request);
  params.insert(params.end(),
                std::make_move_iterator(request.mutable_params()->begin()),
                std::make_move_iterator(request.mutable_params()->end()));

  // Start the VM and build the response.
  ArcVmFeatures features;
  features.rootfs_writable = request.rootfs_writable();
  features.use_dev_conf = !request.ignore_dev_conf();

  if (request.has_balloon_policy()) {
    const auto& policy_params = request.balloon_policy();
    int64_t responsive_max_deflate_bytes =
        policy_params.responsive()
            ? policy_params.responsive_max_deflate_bytes()
            : 0;
    int responsive_timeout_ms =
        policy_params.responsive() ? policy_params.responsive_timeout_ms() : 0;

    if (responsive_max_deflate_bytes <= 0 || responsive_timeout_ms <= 0) {
      // Responsive is enabled, but one of the parameters is not valid.
      // Log a warning and don't enable the responsive balloon policy.
      LOG(WARNING) << "LimitCacheBalloonPolicy is set as responsive, but not "
                   << "all responsive paramters are set.";
      responsive_max_deflate_bytes = 0;
      responsive_timeout_ms = 0;
    }
    features.balloon_policy_params = (LimitCacheBalloonPolicy::Params){
        .reclaim_target_cache = policy_params.reclaim_target_cache(),
        .critical_target_cache = policy_params.critical_target_cache(),
        .moderate_target_cache = policy_params.moderate_target_cache(),
        .responsive_max_deflate_bytes = responsive_max_deflate_bytes};
    if (responsive_timeout_ms > 0) {
      params.emplace_back(base::StringPrintf(
          "androidboot.lmkd.vsock_timeout=%d", responsive_timeout_ms));
    }
  }

  const auto pstore_path = GetPstoreDest(request.owner_id());

  base::FilePath data_dir = base::FilePath(kAndroidDataDir);
  if (!base::PathExists(data_dir)) {
    LOG(WARNING) << "Android data directory does not exist";

    response.set_failure_reason("Android data directory does not exist");
    return response;
  }

  VmId vm_id(request.owner_id(), request.name());
  SendVmStartingUpSignal(vm_id, *vm_info);

  const std::vector<uid_t> privileged_quota_uids = {0};  // Root is privileged.
  std::string shared_data = CreateSharedDataParam(
      data_dir, "_data", true, false, true, privileged_quota_uids);
  std::string shared_data_media = CreateSharedDataParam(
      data_dir, "_data_media", false, true, true, privileged_quota_uids);

  const base::FilePath stub_dir(kStubVolumeSharedDir);
  std::string shared_stub = CreateSharedDataParam(stub_dir, "stub", false, true,
                                                  false, privileged_quota_uids);

  // TOOD(kansho): |non_rt_cpus_num|, |rt_cpus_num| and |affinity|
  // should be passed from chrome instead of |enable_rt_vcpu|.

  // By default we don't request any RT CPUs
  ArcVmCPUTopology topology(request.cpus(), 0);

  // We create only 1 RT VCPU for the time being
  if (request.enable_rt_vcpu())
    topology.SetNumRTCPUs(1);

  topology.CreateCPUAffinity();

  if (request.enable_rt_vcpu()) {
    params.emplace_back("isolcpus=" + topology.RTCPUMask());
    params.emplace_back("androidboot.rtcpus=" + topology.RTCPUMask());
    params.emplace_back("androidboot.non_rtcpus=" + topology.NonRTCPUMask());
  }

  params.emplace_back("ramoops.record_size=" +
                      std::to_string(kArcVmRamoopsRecordSize));
  params.emplace_back("ramoops.console_size=" +
                      std::to_string(kArcVmRamoopsConsoleSize));
  params.emplace_back("ramoops.ftrace_size=" +
                      std::to_string(kArcVmRamoopsFtraceSize));
  params.emplace_back("ramoops.pmsg_size=" +
                      std::to_string(kArcVmRamoopsPmsgSize));
  params.emplace_back("ramoops.dump_oops=1");

  // Customize cache size of squashfs metadata for faster guest OS
  // boot/provisioning.
  params.emplace_back("squashfs.cached_blks=20");

  VmBuilder vm_builder;
  vm_builder.AppendDisks(std::move(disks))
      .SetCpus(topology.NumCPUs())
      .AppendKernelParam(base::JoinString(params, " "))
      .AppendCustomParam("--vcpu-cgroup-path",
                         base::FilePath(kArcvmVcpuCpuCgroup).value())
      .AppendCustomParam("--android-fstab", kFstabPath)
      .AppendCustomParam(
          "--pstore",
          base::StringPrintf("path=%s,size=%" PRId64,
                             pstore_path.value().c_str(), kArcVmRamoopsSize))
      .AppendSharedDir(shared_data)
      .AppendSharedDir(shared_data_media)
      .AppendSharedDir(shared_stub)
      .EnableSmt(false /* enable */)
      .EnablePerVmCoreScheduling(request.use_per_vm_core_scheduling())
      .SetWaylandSocket(request.vm().wayland_server());

  if (request.enable_rt_vcpu()) {
    vm_builder.AppendCustomParam("--rt-cpus", topology.RTCPUMask());
  }

  if (!topology.IsSymmetricCPU() && !topology.AffinityMask().empty()) {
    vm_builder.AppendCustomParam("--cpu-affinity", topology.AffinityMask());
  }

  if (!topology.IsSymmetricCPU() && !topology.CapacityMask().empty()) {
    vm_builder.AppendCustomParam("--cpu-capacity", topology.CapacityMask());
    // Rise the uclamp_min value of the top-app in the ARCVM. This is a
    // performance tuning for games on big.LITTLE platform and Capacity
    // Aware Scheduler (CAS) on Linux.
    vm_builder.AppendKernelParam(base::StringPrintf(
        "androidboot.arc_top_app_uclamp_min=%d", topology.TopAppUclampMin()));
  }

  if (!topology.IsSymmetricCPU() && !topology.PackageMask().empty()) {
    for (auto& package : topology.PackageMask()) {
      vm_builder.AppendCustomParam("--cpu-cluster", package);
    }
  }

  if (request.lock_guest_memory()) {
    vm_builder.AppendCustomParam("--lock-guest-memory", "");
  }

  if (request.use_hugepages()) {
    vm_builder.AppendCustomParam("--hugepages", "");
  }

  vm_builder.SetMemory(std::to_string(GetArcVmMemoryMiB(request)));

  /* Enable THP if the VM has at least 7G of memory */
  if (base::SysInfo::AmountOfPhysicalMemoryMB() >= 7 * 1024) {
    vm_builder.AppendCustomParam("--hugepages", "");
  }

  if (USE_CROSVM_SIBLINGS) {
    vm_builder.SetVmMemoryId(vm_memory_id);
  }

  auto vm = ArcVm::Create(base::FilePath(kKernelPath), vsock_cid,
                          std::move(network_client), std::move(server_proxy),
                          std::move(runtime_dir), std::move(data_disk_path),
                          vm_memory_id, features, std::move(vm_builder));
  if (!vm) {
    LOG(ERROR) << "Unable to start VM";

    response.set_failure_reason("Unable to start VM");
    return response;
  }

  // ARCVM is ready.
  LOG(INFO) << "Started VM with pid " << vm->pid();

  response.set_success(true);
  response.set_status(VM_STATUS_RUNNING);
  vm_info->set_ipv4_address(vm->IPv4Address());
  vm_info->set_pid(vm->pid());

  SendVmStartedSignal(vm_id, *vm_info, response.status());

  vms_[vm_id] = std::move(vm);

  return response;
}

int64_t Service::GetArcVmMemoryMiB(const StartArcVmRequest& request) {
  int64_t memory_mib = request.memory_mib();
  if (memory_mib <= 0) {
    memory_mib = ::vm_tools::concierge::GetVmMemoryMiB();
  }
  return memory_mib;
}

}  // namespace concierge
}  // namespace vm_tools
