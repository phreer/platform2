// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fs;
use std::path::Path;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::thread;
use std::time::Duration;

use anyhow::{bail, Context, Result};

use crate::cgroup::{CgroupCpusetManager, CGROUP_CPUSET_SYSFS, CGROUP_CPUSET_SYSFS_NUM};
use crate::common;
use crate::common::{parse_file_to_u64, FullscreenVideo, RTCAudioActive};
use crate::config;
use crate::config::ConfigProvider;
use crate::feature;
use crate::feature::{CrOSFeature, FeatureProvider};
use crate::power;
use crate::power::PowerPreferencesManager;
use crate::power::PowerSourceProvider;
use featured::{Feature, PlatformFeatures};

use crate::memory::{
    calculate_available_memory_kb, calculate_reserved_free_kb, parse_margins, parse_meminfo,
    parse_psi_memory, total_mem_to_margins_bps, MemInfo,
};

use crate::gpu_freq_scaling::amd_device::AmdDeviceConfig;
use crate::gpu_freq_scaling::{evaluate_gpu_frequency, init_gpu_params, init_gpu_scaling_thread};
use tempfile::{tempdir, TempDir};

#[cfg(target_arch = "x86_64")]
use {crate::power_x86_64, std::arch::x86_64::__cpuid, std::path::PathBuf};

#[test]
fn test_parse_file_to_u64() {
    assert_eq!(
        parse_file_to_u64("123".to_string().as_bytes()).unwrap(),
        123
    );
    assert_eq!(
        parse_file_to_u64("456\n789".to_string().as_bytes()).unwrap(),
        456
    );
    assert!(parse_file_to_u64("".to_string().as_bytes()).is_err());
    assert!(parse_file_to_u64("abc".to_string().as_bytes()).is_err());
}

#[test]
fn test_calculate_reserved_free_kb() {
    let mock_partial_zoneinfo = r#"
Node 0, zone      DMA
  pages free     3968
        min      137
        low      171
        high     205
        spanned  4095
        present  3999
        managed  3976
        protection: (0, 1832, 3000, 3786)
Node 0, zone    DMA32
  pages free     422432
        min      16270
        low      20337
        high     24404
        spanned  1044480
        present  485541
        managed  469149
        protection: (0, 0, 1953, 1500)
Node 0, zone   Normal
  pages free     21708
        min      17383
        low      21728
        high     26073
        spanned  524288
        present  524288
        managed  501235
        protection: (0, 0, 0, 0)"#;
    let page_size_kb = 4;
    let high_watermarks = 205 + 24404 + 26073;
    let lowmem_reserves = 3786 + 1953;
    let reserved = calculate_reserved_free_kb(mock_partial_zoneinfo.as_bytes()).unwrap();
    assert_eq!(reserved, (high_watermarks + lowmem_reserves) * page_size_kb);
}

#[test]
fn test_parse_psi_memory() {
    let mock_psi_memory = r#"
some avg10=57.25 avg60=35.97 avg300=10.18 total=32748793
full avg10=29.29 avg60=19.01 avg300=5.44 total=17589167"#;
    let pressure = parse_psi_memory(mock_psi_memory.as_bytes()).unwrap();
    assert!((pressure - 57.25).abs() < f64::EPSILON);
}

#[test]
fn test_parse_meminfo() {
    let mock_meminfo = r#"
MemTotal:        8025656 kB
MemFree:         4586928 kB
MemAvailable:    6704404 kB
Buffers:          659640 kB
Cached:          1949056 kB
SwapCached:            0 kB
Active:          1430416 kB
Inactive:        1556968 kB
Active(anon):     489640 kB
Inactive(anon):    29188 kB
Active(file):     940776 kB
Inactive(file):  1527780 kB
Unevictable:      151128 kB
Mlocked:           41008 kB
SwapTotal:      11756332 kB
SwapFree:       11756332 kB
Dirty:              5712 kB
Writeback:             0 kB
AnonPages:        529800 kB
Mapped:           321468 kB
Shmem:            140156 kB
Slab:             169252 kB
SReclaimable:     115540 kB
SUnreclaim:        53712 kB
KernelStack:        7072 kB
PageTables:        13340 kB
NFS_Unstable:          0 kB
Bounce:                0 kB
WritebackTmp:          0 kB
CommitLimit:    15769160 kB
Committed_AS:    2483600 kB
VmallocTotal:   34359738367 kB
VmallocUsed:           0 kB
VmallocChunk:          0 kB
Percpu:             2464 kB
AnonHugePages:     40960 kB
ShmemHugePages:        0 kB
ShmemPmdMapped:        0 kB
DirectMap4k:      170216 kB
DirectMap2M:     5992448 kB
DirectMap1G:     3145728 kB"#;
    let meminfo = parse_meminfo(mock_meminfo.as_bytes()).unwrap();
    assert_eq!(meminfo.free, 4586928);
    assert_eq!(meminfo.active_anon, 489640);
    assert_eq!(meminfo.inactive_anon, 29188);
    assert_eq!(meminfo.active_file, 940776);
    assert_eq!(meminfo.inactive_file, 1527780);
    assert_eq!(meminfo.dirty, 5712);
    assert_eq!(meminfo.swap_free, 11756332);
}

#[test]
fn test_calculate_available_memory_kb() {
    let mut info = MemInfo::default();
    let min_filelist = 400 * 1024;
    let reserved_free = 0;
    let ram_swap_weight = 4;

    // Available determined by file cache.
    info.active_file = 500 * 1024;
    info.inactive_file = 500 * 1024;
    info.dirty = 10 * 1024;
    let file = info.active_file + info.inactive_file;
    let available =
        calculate_available_memory_kb(&info, reserved_free, min_filelist, ram_swap_weight);
    assert_eq!(available, file - min_filelist - info.dirty);

    // Available determined by swap free.
    info.swap_free = 1200 * 1024;
    info.active_anon = 1000 * 1024;
    info.inactive_anon = 1000 * 1024;
    info.active_file = 0;
    info.inactive_file = 0;
    info.dirty = 0;
    let available =
        calculate_available_memory_kb(&info, reserved_free, min_filelist, ram_swap_weight);
    assert_eq!(available, info.swap_free / ram_swap_weight);

    // Available determined by anonymous.
    info.swap_free = 6000 * 1024;
    info.active_anon = 500 * 1024;
    info.inactive_anon = 500 * 1024;
    let anon = info.active_anon + info.inactive_anon;
    let available =
        calculate_available_memory_kb(&info, reserved_free, min_filelist, ram_swap_weight);
    assert_eq!(available, anon / ram_swap_weight);

    // When ram_swap_weight is 0, swap is ignored in available.
    info.swap_free = 1200 * 1024;
    info.active_anon = 1000 * 1024;
    info.inactive_anon = 1000 * 1024;
    info.active_file = 500 * 1024;
    info.inactive_file = 500 * 1024;
    let file = info.active_file + info.inactive_file;
    let ram_swap_weight = 0;
    let available =
        calculate_available_memory_kb(&info, reserved_free, min_filelist, ram_swap_weight);
    assert_eq!(available, file - min_filelist);
}

#[test]
fn test_parse_margins() {
    assert!(parse_margins("".to_string().as_bytes()).is_err());
    assert!(parse_margins("123 4a6".to_string().as_bytes()).is_err());
    assert!(parse_margins("123.2 412.3".to_string().as_bytes()).is_err());
    assert!(parse_margins("123".to_string().as_bytes()).is_err());

    let margins = parse_margins("123 456".to_string().as_bytes()).unwrap();
    assert_eq!(margins.len(), 2);
    assert_eq!(margins[0], 123);
    assert_eq!(margins[1], 456);
}

#[test]
fn test_bps_to_margins_bps() {
    let (critical, moderate) = total_mem_to_margins_bps(
        100000, /* 100mb */
        1200,   /* 12% */
        3600,   /* 36% */
    );
    assert_eq!(critical, 12000 /* 12mb */);
    assert_eq!(moderate, 36000 /* 36mb */);

    let (critical, moderate) = total_mem_to_margins_bps(
        1000000, /* 1000mb */
        1250,    /* 12.50% */
        7340,    /* 73.4% */
    );
    assert_eq!(critical, 125000 /* 125mb */);
    assert_eq!(moderate, 734000 /* 734mb */);
}

#[test]
fn test_set_epp() {
    let dir = TempDir::new().unwrap();

    // Create the fake sysfs paths in temp directory
    let mut tpb0 = dir.path().to_owned();
    tpb0.push("sys/devices/system/cpu/cpufreq/policy0/");
    // let dirpath_str0 = tpb0.clone().into_os_string().into_string().unwrap();
    std::fs::create_dir_all(&tpb0).unwrap();

    let mut tpb1 = dir.path().to_owned();
    tpb1.push("sys/devices/system/cpu/cpufreq/policy1/");
    std::fs::create_dir_all(&tpb1).unwrap();

    tpb0.push("energy_performance_preference");
    tpb1.push("energy_performance_preference");

    // Create energy_performance_preference files.
    std::fs::write(&tpb0, "balance_performance").unwrap();
    std::fs::write(&tpb1, "balance_performance").unwrap();

    // Set the EPP
    power::set_epp(dir.path().to_str().unwrap(), "179").unwrap();

    // Verify that files were written
    assert_eq!(std::fs::read_to_string(&tpb0).unwrap(), "179".to_string());
    assert_eq!(std::fs::read_to_string(&tpb1).unwrap(), "179".to_string());
}

#[test]
fn test_amd_device_true() {
    let mock_cpuinfo = r#"
processor	: 0
vendor_id	: AuthenticAMD
cpu family	: 23
model		: 24"#;
    assert!(AmdDeviceConfig::has_amd_tag_in_cpu_info(
        mock_cpuinfo.as_bytes()
    ));
}

#[test]
fn test_amd_device_false() {
    // Incorrect vendor ID
    let mock_cpuinfo = r#"
processor	: 0
vendor_id	: GenuineIntel
cpu family	: 23
model		: 24"#;
    assert!(!AmdDeviceConfig::has_amd_tag_in_cpu_info(
        mock_cpuinfo.as_bytes()
    ));

    // missing vendor ID
    assert!(!AmdDeviceConfig::has_amd_tag_in_cpu_info(
        "".to_string().as_bytes()
    ));
    assert!(!AmdDeviceConfig::has_amd_tag_in_cpu_info(
        "processor: 0".to_string().as_bytes()
    ));
}

#[test]
fn test_amd_parse_sclk_valid() {
    let dev: AmdDeviceConfig = AmdDeviceConfig::new("mock_file", "mock_sclk");

    // trailing space is intentional, reflects sysfs output.
    let mock_sclk = r#"
0: 200Mhz 
1: 700Mhz *
2: 1400Mhz "#;

    let (sclk, sel) = dev.parse_sclk(mock_sclk.as_bytes()).unwrap();
    assert_eq!(1, sel);
    assert_eq!(3, sclk.len());
    assert_eq!(200, sclk[0]);
    assert_eq!(700, sclk[1]);
    assert_eq!(1400, sclk[2]);
}

#[test]
fn test_amd_parse_sclk_invalid() {
    let dev: AmdDeviceConfig = AmdDeviceConfig::new("mock_file", "mock_sclk");

    // trailing space is intentional, reflects sysfs output.
    let mock_sclk = r#"
0: nonint 
1: 700Mhz *
2: 1400Mhz "#;
    assert!(dev.parse_sclk(mock_sclk.as_bytes()).is_err());
    assert!(dev.parse_sclk("nonint".to_string().as_bytes()).is_err());
    assert!(dev.parse_sclk("0: 1400 ".to_string().as_bytes()).is_err());
    assert!(dev.parse_sclk("0: 1400 *".to_string().as_bytes()).is_err());
    assert!(dev
        .parse_sclk("x: nonint *".to_string().as_bytes())
        .is_err());
}

#[test]
fn test_amd_device_filter_pass() {
    let dev: AmdDeviceConfig = AmdDeviceConfig::new("mock_file", "mock_sclk");

    let mock_cpuinfo = r#"
processor	: 0
vendor_id	: AuthenticAMD
cpu family	: 23
model		: 24
model name	: AMD Ryzen 7 3700C  with Radeon Vega Mobile Gfx
stepping	: 1
microcode	: 0x8108109"#;

    assert!(dev
        .is_supported_dev_family(mock_cpuinfo.as_bytes())
        .unwrap());
    assert!(dev
        .is_supported_dev_family("model name	: AMD Ryzen 5 3700C".as_bytes())
        .unwrap());
}

#[test]
fn test_amd_device_filter_fail() {
    let dev: AmdDeviceConfig = AmdDeviceConfig::new("mock_file", "mock_sclk");

    let mock_cpuinfo = r#"
processor	: 0
vendor_id	: AuthenticAMD
cpu family	: 23
model		: 24
model name	: AMD Ryzen 3 3700C  with Radeon Vega Mobile Gfx
stepping	: 1
microcode	: 0x8108109"#;

    assert!(!dev
        .is_supported_dev_family(mock_cpuinfo.as_bytes())
        .unwrap());
    assert!(!dev
        .is_supported_dev_family("model name	: AMD Ryzen 5 2700C".as_bytes())
        .unwrap());
    assert!(!dev
        .is_supported_dev_family("model name	: AMD Ryzen 3 3700C".as_bytes())
        .unwrap());
    assert!(!dev
        .is_supported_dev_family("model name	: malformed".as_bytes())
        .unwrap());
    assert!(!dev.is_supported_dev_family("".as_bytes()).unwrap());
}

#[test]
#[allow(unused_must_use)]
fn test_gpu_thread_on_off() {
    println!("test gpu thread");
    let config = init_gpu_params().unwrap();

    // TODO: break this function to make is unit testable
    evaluate_gpu_frequency(&config, 150);
    let game_mode_on = Arc::new(AtomicBool::new(true));
    let game_mode_on_clone = Arc::clone(&game_mode_on);

    init_gpu_scaling_thread(game_mode_on_clone, 1000);
    thread::sleep(Duration::from_millis(500));
    game_mode_on.store(false, Ordering::Relaxed);
    thread::sleep(Duration::from_millis(500));

    println!("gpu thread exit gracefully");
}

#[test]
fn test_config_provider_empty_root() -> Result<()> {
    let root = tempdir()?;
    let provider = config::DirectoryConfigProvider {
        root: root.path().to_path_buf(),
    };

    let preference = provider.read_power_preferences(
        config::PowerSourceType::AC,
        config::PowerPreferencesType::Default,
    )?;

    assert!(preference.is_none());

    let preference = provider.read_power_preferences(
        config::PowerSourceType::DC,
        config::PowerPreferencesType::Default,
    )?;

    assert!(preference.is_none());

    Ok(())
}

#[test]
fn test_config_provider_empty_dir() -> Result<()> {
    let root = tempdir()?;
    let path = root.path().join(config::RESOURCED_CONFIG_PATH);
    fs::create_dir_all(path).unwrap();

    let provider = config::DirectoryConfigProvider {
        root: root.path().to_path_buf(),
    };

    let preference = provider.read_power_preferences(
        config::PowerSourceType::AC,
        config::PowerPreferencesType::Default,
    )?;

    assert!(preference.is_none());

    let preference = provider.read_power_preferences(
        config::PowerSourceType::DC,
        config::PowerPreferencesType::Default,
    )?;

    assert!(preference.is_none());

    Ok(())
}

#[test]
fn test_config_provider_ondemand_all_types() -> Result<()> {
    let power_source_params = [
        (config::PowerSourceType::AC, "ac"),
        (config::PowerSourceType::DC, "dc"),
    ];

    let preference_params = [
        (
            config::PowerPreferencesType::Default,
            "default-power-preferences",
        ),
        (
            config::PowerPreferencesType::WebRTC,
            "web-rtc-power-preferences",
        ),
        (
            config::PowerPreferencesType::Fullscreen,
            "fullscreen-power-preferences",
        ),
        (
            config::PowerPreferencesType::Gaming,
            "gaming-power-preferences",
        ),
    ];

    for (power_source, power_source_path) in power_source_params {
        for (preference, preference_path) in preference_params {
            let root = tempdir()?;
            let ondemand_path = root
                .path()
                .join(config::RESOURCED_CONFIG_PATH)
                .join(power_source_path)
                .join(preference_path)
                .join("governor")
                .join("ondemand");
            fs::create_dir_all(&ondemand_path)?;

            let powersave_bias_path = ondemand_path.join("powersave-bias");
            fs::write(&powersave_bias_path, b"340")?;

            let provider = config::DirectoryConfigProvider {
                root: root.path().to_path_buf(),
            };

            let actual = provider.read_power_preferences(power_source, preference)?;

            let expected = config::PowerPreferences {
                governor: Some(config::Governor::OndemandGovernor {
                    powersave_bias: 340,
                    sampling_rate: None,
                }),
            };

            assert_eq!(expected, actual.unwrap());

            // Now try with a sampling_rate 0 (unset)

            let powersave_bias_path = ondemand_path.join("sampling-rate-ms");
            fs::write(&powersave_bias_path, b"0")?;

            let actual = provider.read_power_preferences(power_source, preference)?;

            let expected = config::PowerPreferences {
                governor: Some(config::Governor::OndemandGovernor {
                    powersave_bias: 340,
                    sampling_rate: None,
                }),
            };

            assert_eq!(expected, actual.unwrap());

            // Now try with a sampling_rate 16

            let powersave_bias_path = ondemand_path.join("sampling-rate-ms");
            fs::write(&powersave_bias_path, b"16")?;

            let actual = provider.read_power_preferences(power_source, preference)?;

            let expected = config::PowerPreferences {
                governor: Some(config::Governor::OndemandGovernor {
                    powersave_bias: 340,
                    sampling_rate: Some(16000),
                }),
            };

            assert_eq!(expected, actual.unwrap());
        }
    }

    Ok(())
}

#[test]
fn test_power_source_provider_empty_root() -> Result<()> {
    let root = tempdir()?;

    let provider = power::DirectoryPowerSourceProvider {
        root: root.path().to_path_buf(),
    };

    let power_source = provider.get_power_source()?;

    assert_eq!(power_source, config::PowerSourceType::DC);

    Ok(())
}

const POWER_SUPPLY_PATH: &str = "sys/class/power_supply";

#[test]
fn test_power_source_provider_empty_path() -> Result<()> {
    let root = tempdir()?;

    let path = root.path().join(POWER_SUPPLY_PATH);
    fs::create_dir_all(&path)?;

    let provider = power::DirectoryPowerSourceProvider {
        root: root.path().to_path_buf(),
    };

    let power_source = provider.get_power_source()?;

    assert_eq!(power_source, config::PowerSourceType::DC);

    Ok(())
}

/// Tests that the `DirectoryPowerSourceProvider` can parse the charger sysfs
/// `online` and `status` attributes.
#[test]
fn test_power_source_provider_disconnected_then_connected() -> Result<()> {
    let root = tempdir()?;

    let path = root.path().join(POWER_SUPPLY_PATH);
    fs::create_dir_all(&path)?;

    let provider = power::DirectoryPowerSourceProvider {
        root: root.path().to_path_buf(),
    };

    let charger = path.join("charger-1");
    fs::create_dir_all(&charger)?;
    let online = charger.join("online");

    fs::write(&online, b"0")?;
    let power_source = provider.get_power_source()?;
    assert_eq!(power_source, config::PowerSourceType::DC);

    let status = charger.join("status");
    fs::write(&online, b"1")?;
    fs::write(&status, b"Charging\n")?;
    let power_source = provider.get_power_source()?;
    assert_eq!(power_source, config::PowerSourceType::AC);

    fs::write(&online, b"1")?;
    fs::write(&status, b"Not Charging\n")?;
    let power_source = provider.get_power_source()?;
    assert_eq!(power_source, config::PowerSourceType::DC);

    Ok(())
}

struct FakeConfigProvider {
    default_power_preferences:
        fn(config::PowerSourceType) -> Result<Option<config::PowerPreferences>>,
    web_rtc_power_preferences:
        fn(config::PowerSourceType) -> Result<Option<config::PowerPreferences>>,
    fullscreen_power_preferences:
        fn(config::PowerSourceType) -> Result<Option<config::PowerPreferences>>,
    gaming_power_preferences:
        fn(config::PowerSourceType) -> Result<Option<config::PowerPreferences>>,
}

impl Default for FakeConfigProvider {
    fn default() -> FakeConfigProvider {
        FakeConfigProvider {
            // We bail on default to ensure the tests correctly setup a default power preference.
            default_power_preferences: |_| bail!("Default not Implemented"),
            web_rtc_power_preferences: |_| bail!("WebRTC not Implemented"),
            fullscreen_power_preferences: |_| bail!("Fullscreen not Implemented"),
            gaming_power_preferences: |_| bail!("Gaming not Implemented"),
        }
    }
}

impl config::ConfigProvider for FakeConfigProvider {
    fn read_power_preferences(
        &self,
        power_source_type: config::PowerSourceType,
        power_preference_type: config::PowerPreferencesType,
    ) -> Result<Option<config::PowerPreferences>> {
        match power_preference_type {
            config::PowerPreferencesType::Default => {
                (self.default_power_preferences)(power_source_type)
            }
            config::PowerPreferencesType::WebRTC => {
                (self.web_rtc_power_preferences)(power_source_type)
            }
            config::PowerPreferencesType::Fullscreen => {
                (self.fullscreen_power_preferences)(power_source_type)
            }
            config::PowerPreferencesType::Gaming => {
                (self.gaming_power_preferences)(power_source_type)
            }
        }
    }
}

struct FakePowerSourceProvider {
    power_source: config::PowerSourceType,
}

impl power::PowerSourceProvider for FakePowerSourceProvider {
    fn get_power_source(&self) -> Result<config::PowerSourceType> {
        Ok(self.power_source)
    }
}

fn write_powersave_bias(root: &Path, value: u32) -> Result<()> {
    let ondemand_path = root.join("sys/devices/system/cpu/cpufreq/ondemand");
    fs::create_dir_all(&ondemand_path)?;

    std::fs::write(ondemand_path.join("powersave_bias"), value.to_string())?;

    Ok(())
}

fn read_powersave_bias(root: &Path) -> Result<String> {
    let powersave_bias_path = root
        .join("sys/devices/system/cpu/cpufreq/ondemand")
        .join("powersave_bias");

    let powersave_bias = std::fs::read_to_string(powersave_bias_path)?;

    Ok(powersave_bias)
}

fn write_sampling_rate(root: &Path, value: u32) -> Result<()> {
    let ondemand_path = root.join("sys/devices/system/cpu/cpufreq/ondemand");
    fs::create_dir_all(&ondemand_path)?;

    std::fs::write(ondemand_path.join("sampling_rate"), value.to_string())?;

    Ok(())
}

fn read_sampling_rate(root: &Path) -> Result<String> {
    let sampling_rate_path = root
        .join("sys/devices/system/cpu/cpufreq/ondemand")
        .join("sampling_rate");

    let sampling_rate = std::fs::read_to_string(sampling_rate_path)?;

    Ok(sampling_rate)
}

fn write_epp(root: &Path, value: &str) -> Result<()> {
    let policy_path = root.join("sys/devices/system/cpu/cpufreq/policy0");
    fs::create_dir_all(&policy_path)?;

    std::fs::write(policy_path.join("energy_performance_preference"), value)?;

    Ok(())
}

fn read_epp(root: &Path) -> Result<String> {
    let epp_path = root
        .join("sys/devices/system/cpu/cpufreq/policy0/")
        .join("energy_performance_preference");

    let epp = std::fs::read_to_string(epp_path)?;

    Ok(epp)
}

#[test]
fn test_power_update_power_preferences_wrong_governor() -> Result<()> {
    let root = tempdir()?;

    let power_source_provider = FakePowerSourceProvider {
        power_source: config::PowerSourceType::AC,
    };

    let config_provider = FakeConfigProvider {
        default_power_preferences: |_| {
            Ok(Some(config::PowerPreferences {
                governor: Some(config::Governor::OndemandGovernor {
                    powersave_bias: 200,
                    sampling_rate: None,
                }),
            }))
        },
        ..Default::default()
    };

    let feature_provider = FakeCrOSFeatureProvider::new()?;

    create_fake_cgroup_cpuset_sysfs(root.path())?;
    let cpuset_manager = CgroupCpusetManager::new(root.path().to_path_buf())?;

    let manager = power::DirectoryPowerPreferencesManager {
        root: root.path().to_path_buf(),
        config_provider,
        power_source_provider,
        feature_provider,
        cpuset_manager,
    };

    manager.update_power_preferences(
        common::RTCAudioActive::Inactive,
        common::FullscreenVideo::Inactive,
        common::GameMode::Off,
    )?;

    // We shouldn't have written anything.
    let powersave_bias = read_powersave_bias(root.path());
    assert!(powersave_bias.is_err());

    Ok(())
}

#[test]
fn test_power_update_power_preferences_none() -> Result<()> {
    let root = tempdir()?;

    write_powersave_bias(root.path(), 0)?;
    write_sampling_rate(root.path(), 2000)?;

    let power_source_provider = FakePowerSourceProvider {
        power_source: config::PowerSourceType::AC,
    };

    let config_provider = FakeConfigProvider {
        default_power_preferences: |_| Ok(None),
        ..Default::default()
    };

    let feature_provider = FakeCrOSFeatureProvider::new()?;

    create_fake_cgroup_cpuset_sysfs(root.path())?;
    let cpuset_manager = CgroupCpusetManager::new(root.path().to_path_buf())?;

    let manager = power::DirectoryPowerPreferencesManager {
        root: root.path().to_path_buf(),
        config_provider,
        power_source_provider,
        feature_provider,
        cpuset_manager,
    };

    manager.update_power_preferences(
        common::RTCAudioActive::Inactive,
        common::FullscreenVideo::Inactive,
        common::GameMode::Off,
    )?;

    let powersave_bias = read_powersave_bias(root.path())?;
    assert_eq!(powersave_bias, "0");

    let sampling_rate = read_sampling_rate(root.path())?;
    assert_eq!(sampling_rate, "2000");

    Ok(())
}

#[test]
fn test_power_update_power_preferences_default_ac() -> Result<()> {
    let root = tempdir()?;

    write_powersave_bias(root.path(), 0)?;
    write_sampling_rate(root.path(), 2000)?;

    let power_source_provider = FakePowerSourceProvider {
        power_source: config::PowerSourceType::AC,
    };

    let config_provider = FakeConfigProvider {
        default_power_preferences: |power_source| {
            assert_eq!(power_source, config::PowerSourceType::AC);

            Ok(Some(config::PowerPreferences {
                governor: Some(config::Governor::OndemandGovernor {
                    powersave_bias: 200,
                    sampling_rate: Some(16000),
                }),
            }))
        },
        ..Default::default()
    };

    let feature_provider = FakeCrOSFeatureProvider::new()?;

    create_fake_cgroup_cpuset_sysfs(root.path())?;
    let cpuset_manager = CgroupCpusetManager::new(root.path().to_path_buf())?;

    let manager = power::DirectoryPowerPreferencesManager {
        root: root.path().to_path_buf(),
        config_provider,
        power_source_provider,
        feature_provider,
        cpuset_manager,
    };

    manager.update_power_preferences(
        common::RTCAudioActive::Inactive,
        common::FullscreenVideo::Inactive,
        common::GameMode::Off,
    )?;

    let powersave_bias = read_powersave_bias(root.path())?;
    assert_eq!(powersave_bias, "200");

    let sampling_rate = read_sampling_rate(root.path())?;
    assert_eq!(sampling_rate, "16000");

    Ok(())
}

#[test]
fn test_power_update_power_preferences_default_dc() -> Result<()> {
    let root = tempdir()?;

    write_powersave_bias(root.path(), 0)?;
    write_sampling_rate(root.path(), 2000)?;

    let power_source_provider = FakePowerSourceProvider {
        power_source: config::PowerSourceType::DC,
    };

    let config_provider = FakeConfigProvider {
        default_power_preferences: |power_source| {
            assert_eq!(power_source, config::PowerSourceType::DC);

            Ok(Some(config::PowerPreferences {
                governor: Some(config::Governor::OndemandGovernor {
                    powersave_bias: 200,
                    sampling_rate: None,
                }),
            }))
        },
        ..Default::default()
    };

    let feature_provider = FakeCrOSFeatureProvider::new()?;

    create_fake_cgroup_cpuset_sysfs(root.path())?;
    let cpuset_manager = CgroupCpusetManager::new(root.path().to_path_buf())?;

    let manager = power::DirectoryPowerPreferencesManager {
        root: root.path().to_path_buf(),
        config_provider,
        power_source_provider,
        feature_provider,
        cpuset_manager,
    };

    manager.update_power_preferences(
        common::RTCAudioActive::Inactive,
        common::FullscreenVideo::Inactive,
        common::GameMode::Off,
    )?;

    let powersave_bias = read_powersave_bias(root.path())?;
    assert_eq!(powersave_bias, "200");

    let sampling_rate = read_sampling_rate(root.path())?;
    assert_eq!(sampling_rate, "2000");

    Ok(())
}

#[test]
fn test_power_update_power_preferences_default_rtc_active() -> Result<()> {
    let root = tempdir()?;

    write_powersave_bias(root.path(), 0)?;
    write_sampling_rate(root.path(), 2000)?;

    let power_source_provider = FakePowerSourceProvider {
        power_source: config::PowerSourceType::AC,
    };

    let config_provider = FakeConfigProvider {
        default_power_preferences: |_| {
            Ok(Some(config::PowerPreferences {
                governor: Some(config::Governor::OndemandGovernor {
                    powersave_bias: 200,
                    sampling_rate: Some(4000),
                }),
            }))
        },
        web_rtc_power_preferences: |_| Ok(None),
        ..Default::default()
    };

    let feature_provider = FakeCrOSFeatureProvider::new()?;

    create_fake_cgroup_cpuset_sysfs(root.path())?;
    let cpuset_manager = CgroupCpusetManager::new(root.path().to_path_buf())?;

    let manager = power::DirectoryPowerPreferencesManager {
        root: root.path().to_path_buf(),
        config_provider,
        power_source_provider,
        feature_provider,
        cpuset_manager,
    };

    manager.update_power_preferences(
        common::RTCAudioActive::Active,
        common::FullscreenVideo::Inactive,
        common::GameMode::Off,
    )?;

    let powersave_bias = read_powersave_bias(root.path())?;
    assert_eq!(powersave_bias, "200");

    let sampling_rate = read_sampling_rate(root.path())?;
    assert_eq!(sampling_rate, "4000");

    Ok(())
}

#[test]
fn test_power_update_power_preferences_rtc_active() -> Result<()> {
    let root = tempdir()?;

    write_powersave_bias(root.path(), 0)?;
    write_sampling_rate(root.path(), 2000)?;

    let power_source_provider = FakePowerSourceProvider {
        power_source: config::PowerSourceType::AC,
    };

    let config_provider = FakeConfigProvider {
        web_rtc_power_preferences: |_| {
            Ok(Some(config::PowerPreferences {
                governor: Some(config::Governor::OndemandGovernor {
                    powersave_bias: 200,
                    sampling_rate: Some(16000),
                }),
            }))
        },
        ..Default::default()
    };

    let feature_provider = FakeCrOSFeatureProvider::new()?;

    create_fake_cgroup_cpuset_sysfs(root.path())?;
    let cpuset_manager = CgroupCpusetManager::new(root.path().to_path_buf())?;

    let manager = power::DirectoryPowerPreferencesManager {
        root: root.path().to_path_buf(),
        config_provider,
        power_source_provider,
        feature_provider,
        cpuset_manager,
    };

    manager.update_power_preferences(
        common::RTCAudioActive::Active,
        common::FullscreenVideo::Inactive,
        common::GameMode::Off,
    )?;

    let powersave_bias = read_powersave_bias(root.path())?;
    assert_eq!(powersave_bias, "200");

    let sampling_rate = read_sampling_rate(root.path())?;
    assert_eq!(sampling_rate, "16000");

    Ok(())
}

#[test]
/// Tests the various EPP permutations
fn test_power_update_power_preferences_epp() -> Result<()> {
    let root = tempdir()?;

    write_epp(root.path(), "balance_performance")?;

    let power_source_provider = FakePowerSourceProvider {
        power_source: config::PowerSourceType::AC,
    };

    // Let's assume we have no config
    let config_provider = FakeConfigProvider {
        default_power_preferences: |_| Ok(None),
        web_rtc_power_preferences: |_| Ok(None),
        fullscreen_power_preferences: |_| Ok(None),
        ..Default::default()
    };

    let feature_provider = FakeCrOSFeatureProvider::new()?;

    create_fake_cgroup_cpuset_sysfs(root.path())?;
    let cpuset_manager = CgroupCpusetManager::new(root.path().to_path_buf())?;

    let manager = power::DirectoryPowerPreferencesManager {
        root: root.path().to_path_buf(),
        config_provider,
        power_source_provider,
        feature_provider,
        cpuset_manager,
    };

    let tests = [
        (RTCAudioActive::Active, FullscreenVideo::Inactive, "179"),
        (RTCAudioActive::Inactive, FullscreenVideo::Active, "179"),
        (RTCAudioActive::Active, FullscreenVideo::Active, "179"),
        (
            RTCAudioActive::Inactive,
            FullscreenVideo::Inactive,
            "balance_performance",
        ),
    ];

    for test in tests {
        manager.update_power_preferences(test.0, test.1, common::GameMode::Off)?;

        let epp = read_epp(root.path())?;

        assert_eq!(epp, test.2);
    }

    Ok(())
}

#[test]
fn test_power_update_power_preferences_fullscreen_active() -> Result<()> {
    let root = tempdir()?;

    write_powersave_bias(root.path(), 0)?;
    write_sampling_rate(root.path(), 2000)?;

    let power_source_provider = FakePowerSourceProvider {
        power_source: config::PowerSourceType::AC,
    };

    let config_provider = FakeConfigProvider {
        fullscreen_power_preferences: |_| {
            Ok(Some(config::PowerPreferences {
                governor: Some(config::Governor::OndemandGovernor {
                    powersave_bias: 200,
                    sampling_rate: Some(16000),
                }),
            }))
        },
        ..Default::default()
    };

    let feature_provider = FakeCrOSFeatureProvider::new()?;

    create_fake_cgroup_cpuset_sysfs(root.path())?;
    let cpuset_manager = CgroupCpusetManager::new(root.path().to_path_buf())?;

    let manager = power::DirectoryPowerPreferencesManager {
        root: root.path().to_path_buf(),
        config_provider,
        power_source_provider,
        feature_provider,
        cpuset_manager,
    };

    manager.update_power_preferences(
        common::RTCAudioActive::Inactive,
        common::FullscreenVideo::Active,
        common::GameMode::Off,
    )?;

    let powersave_bias = read_powersave_bias(root.path())?;
    assert_eq!(powersave_bias, "200");

    let sampling_rate = read_sampling_rate(root.path())?;
    assert_eq!(sampling_rate, "16000");

    Ok(())
}

#[test]
fn test_power_update_power_preferences_gaming_active() -> Result<()> {
    let root = tempdir()?;

    write_powersave_bias(root.path(), 0)?;
    write_sampling_rate(root.path(), 2000)?;

    let power_source_provider = FakePowerSourceProvider {
        power_source: config::PowerSourceType::AC,
    };

    let config_provider = FakeConfigProvider {
        gaming_power_preferences: |_| {
            Ok(Some(config::PowerPreferences {
                governor: Some(config::Governor::OndemandGovernor {
                    powersave_bias: 200,
                    sampling_rate: Some(16000),
                }),
            }))
        },
        ..Default::default()
    };

    let feature_provider = FakeCrOSFeatureProvider::new()?;

    create_fake_cgroup_cpuset_sysfs(root.path())?;
    let cpuset_manager = CgroupCpusetManager::new(root.path().to_path_buf())?;

    let manager = power::DirectoryPowerPreferencesManager {
        root: root.path().to_path_buf(),
        config_provider,
        power_source_provider,
        feature_provider,
        cpuset_manager,
    };

    manager.update_power_preferences(
        common::RTCAudioActive::Inactive,
        common::FullscreenVideo::Inactive,
        common::GameMode::Borealis,
    )?;

    let powersave_bias = read_powersave_bias(root.path())?;
    assert_eq!(powersave_bias, "200");

    let sampling_rate = read_sampling_rate(root.path())?;
    assert_eq!(sampling_rate, "16000");

    Ok(())
}

fn write_i32_to_file(path: &Path, value: i32) -> Result<()> {
    fs::create_dir_all(
        path.parent()
            .with_context(|| format!("cannot get parent: {}", path.display()))?,
    )?;

    std::fs::write(path, value.to_string())?;
    Ok(())
}

#[test]
fn test_set_gt_boost_freq_mhz() -> Result<()> {
    const GT_BOOST_FREQ_MHZ_PATH: &str = "sys/class/drm/card0/gt_boost_freq_mhz";
    const GT_MAX_FREQ_MHZ_PATH: &str = "sys/class/drm/card0/gt_max_freq_mhz";
    const GT_MIN_FREQ_MHZ_PATH: &str = "sys/class/drm/card0/gt_min_freq_mhz";

    let root = tempdir()?;
    let root_path = root.path();
    let gt_boost_freq_mhz_path = root_path.join(GT_BOOST_FREQ_MHZ_PATH);
    let gt_max_freq_mhz_path = root_path.join(GT_MAX_FREQ_MHZ_PATH);
    let gt_min_freq_mhz_path = root_path.join(GT_MIN_FREQ_MHZ_PATH);
    write_i32_to_file(&gt_boost_freq_mhz_path, 500)?;
    write_i32_to_file(&gt_max_freq_mhz_path, 1100)?;
    write_i32_to_file(&gt_min_freq_mhz_path, 300)?;

    common::set_gt_boost_freq_mhz_impl(root_path, common::RTCAudioActive::Active)?;

    assert_eq!(common::read_file_to_u64(&gt_boost_freq_mhz_path)?, 300);

    common::set_gt_boost_freq_mhz_impl(root_path, common::RTCAudioActive::Inactive)?;

    assert_eq!(common::read_file_to_u64(&gt_boost_freq_mhz_path)?, 1100);

    Ok(())
}

pub struct FakeCrOSFeatureProvider<'a> {
    pub fake_feature_list: Vec<CrOSFeature<'a>>,
    platform_features: PlatformFeatures,
}

impl FakeCrOSFeatureProvider<'_> {
    pub fn new() -> Result<Self> {
        let mut fake_feature_list = Vec::new();

        let feature = Feature::new("CrOSLateBootKnownFeature1", false).unwrap();
        fake_feature_list.push(CrOSFeature {
            name: "CrOSLateBootKnownFeature1",
            enabled_by_default: false,
            feature: Some(feature),
        });

        let feature = Feature::new("CrOSLateBootMediaDynamicCgroup", false).unwrap();
        fake_feature_list.push(CrOSFeature {
            name: "CrOSLateBootMediaDynamicCgroup",
            enabled_by_default: false,
            feature: Some(feature),
        });

        Ok(FakeCrOSFeatureProvider {
            fake_feature_list,
            platform_features: PlatformFeatures::new()?,
        })
    }
}

impl feature::FeatureProvider for FakeCrOSFeatureProvider<'_> {
    fn feature_enabled(&self, name: &str) -> Result<bool> {
        match self
            .fake_feature_list
            .iter()
            .find(|&feature| feature.name == name)
        {
            Some(feature_item) => feature_item.feature_enabled(&self.platform_features),
            None => bail!("Not able to find Feature! {}", &name),
        }
    }
}

#[test]
fn test_feature_provider() -> Result<()> {
    let feature_provider = FakeCrOSFeatureProvider::new()?;

    // Below function must return Error since it's unknown feature to resourced.
    assert!(feature_provider
        .feature_enabled("CrOSLateBootUnknownFeature1")
        .is_err());

    // Below function must return SUCCESS since it's in the fake_feature_list.
    assert!(feature_provider
        .feature_enabled("CrOSLateBootMediaDynamicCgroup")
        .is_ok());
    Ok(())
}

#[cfg(test)]
static FAKE_CROS_FEATURE_EXISTS: AtomicBool = AtomicBool::new(false);

pub struct FakeCrOSFeatureProviderSingleton<'a> {
    pub fake_feature_list: Vec<CrOSFeature<'a>>,
    platform_features: PlatformFeatures,
}

impl FakeCrOSFeatureProviderSingleton<'_> {
    pub fn new() -> Result<Self> {
        // Check this is the first time get called.
        match FAKE_CROS_FEATURE_EXISTS.compare_exchange(
            false,
            true,
            Ordering::Relaxed,
            Ordering::Relaxed,
        ) {
            // Populate fake_feature_list if this is the very first instance.
            Ok(_) => {
                let mut fake_feature_list = Vec::new();

                let feature = Feature::new("CrOSLateBootKnownFeature1", false).unwrap();
                fake_feature_list.push(CrOSFeature {
                    name: "CrOSLateBootKnownFeature1",
                    enabled_by_default: false,
                    feature: Some(feature),
                });

                let feature = Feature::new("CrOSLateBootMediaDynamicCgroup", false).unwrap();
                fake_feature_list.push(CrOSFeature {
                    name: "CrOSLateBootMediaDynamicCgroup",
                    enabled_by_default: false,
                    feature: Some(feature),
                });
                Ok(FakeCrOSFeatureProviderSingleton {
                    fake_feature_list,
                    platform_features: PlatformFeatures::new()?,
                })
            }
            // Create empty feature_list if this is not the first time.
            // This is to ensure that feature_list is being created only once.
            Err(_) => bail!("Failed to create CrOSFeatureProvider since it already exitis!"),
        }
    }
}

impl Drop for FakeCrOSFeatureProviderSingleton<'_> {
    fn drop(&mut self) {
        FAKE_CROS_FEATURE_EXISTS.store(false, Ordering::Relaxed)
    }
}

impl feature::FeatureProvider for FakeCrOSFeatureProviderSingleton<'_> {
    fn feature_enabled(&self, name: &str) -> Result<bool> {
        match self
            .fake_feature_list
            .iter()
            .find(|&feature| feature.name == name)
        {
            Some(feature_item) => feature_item.feature_enabled(&self.platform_features),
            None => bail!("Not able to find Feature! {}", &name),
        }
    }
}

#[test]
fn test_feature_provider_singleton() -> Result<()> {
    let feature_provider1 = FakeCrOSFeatureProviderSingleton::new()?;

    // Below function must return SUCCESS since it's in the fake_feature_list.
    assert!(feature_provider1
        .feature_enabled("CrOSLateBootKnownFeature1")
        .is_ok());

    // Below function must return SUCCESS since it's in the fake_feature_list.
    assert!(feature_provider1
        .feature_enabled("CrOSLateBootMediaDynamicCgroup")
        .is_ok());

    // Below function must fail since it's NOT in the fake_feature_list.
    assert!(feature_provider1
        .feature_enabled("CrOSLateBootUnKnownFeature1")
        .is_err());

    // Below function must fail since this is the 2nd instance of feature_provider.
    let feature_provider2 = FakeCrOSFeatureProviderSingleton::new();
    assert!(feature_provider2.is_err());

    Ok(())
}

const FAKE_CPUSETS: [(&str, &str); CGROUP_CPUSET_SYSFS_NUM] = [
    ("sys/fs/cgroup/cpuset/chrome/urgent/", "0-3"),
    ("sys/fs/cgroup/cpuset/chrome/non-urgent/", "4-11"),
    ("sys/fs/cgroup/cpuset/chrome/", "0-11"),
    ("sys/fs/cgroup/cpuset/media/", "4-11"),
];

fn create_fake_cgroup_cpuset_sysfs(root: &Path) -> Result<()> {
    // Create fake platform cgroup/cpuset sysfs.
    for entry in FAKE_CPUSETS.iter() {
        let mut cgroup_sysfs = root.to_owned();

        // Create fake cgroup/cpuset directory.
        cgroup_sysfs.push(entry.0);
        std::fs::create_dir_all(&cgroup_sysfs).unwrap();

        // Create fake sysfs cgroup/cpuset/../cpus and set the default cpus.
        std::fs::write(cgroup_sysfs.join("cpus"), &entry.1)?;
    }
    Ok(())
}

#[test]
fn test_cgroup_cpuset_manager() -> Result<()> {
    // Create temp directory.
    let root = tempdir()?;

    // Create fake cgroup/cpuset sysfs.
    create_fake_cgroup_cpuset_sysfs(root.path())?;

    // Create CgroupCpusetManger.
    let cpuset_manager = CgroupCpusetManager::new(root.path().to_path_buf())?;

    // Write all fake cgroup/cpuset/../cpus sysfs with "4-7".
    cpuset_manager.write_all("4-7")?;

    // Read fake cgroup/cpuset/../cpus sysfs and check all cpus is "4-7".
    for entry in CGROUP_CPUSET_SYSFS.iter() {
        let sysfs = root.path().to_owned().join(entry);
        assert_eq!(std::fs::read_to_string(&sysfs).unwrap(), "4-7");
    }

    // Write back platform's defalut cpus to fake cgroup/cpuset/../cpus sysfs.
    cpuset_manager.restore_all()?;

    // Read fake cgroup/cpuset/../cpus sysfs and check all cpus has default/initial cpus.
    for (index, entry) in CGROUP_CPUSET_SYSFS.iter().enumerate() {
        let sysfs = root.path().to_owned().join(entry);
        assert_eq!(
            std::fs::read_to_string(&sysfs).unwrap(),
            FAKE_CPUSETS[index].1
        );
    }

    Ok(())
}

#[test]
#[cfg(target_arch = "x86_64")]
fn test_power_is_intel_hybrid_system() -> Result<()> {
    // cpuid with EAX=0: Highest Function Parameter and Manufacturer ID.
    // This returns the CPU's manufacturer ID string.
    // The largest value that EAX can be set to before calling CPUID is returned in EAX.
    // https://en.wikipedia.org/wiki/CPUID.
    const CPUID_EAX_FOR_HFP_MID: u32 = 0;

    // Intel processor manufacture ID is "GenuineIntel".
    const CPUID_GENUINE_INTEL_EBX: u32 = 0x756e6547;
    const CPUID_GENUINE_INTEL_ECX: u32 = 0x6c65746e;
    const CPUID_GENUINE_INTEL_EDX: u32 = 0x49656e69;
    const CPUID_EAX_EXT_FEATURE: u32 = 7;

    let mut intel_platform = false;
    let mut highest_feature = 0;

    // Read highest function parameter and manufacturer ID.
    let processor_info = unsafe { __cpuid(CPUID_EAX_FOR_HFP_MID) };

    // Check system has Intel platform i.e "GenuineIntel".
    if processor_info.ebx == CPUID_GENUINE_INTEL_EBX
        && processor_info.ecx == CPUID_GENUINE_INTEL_ECX
        && processor_info.edx == CPUID_GENUINE_INTEL_EDX
    {
        intel_platform = true;
        highest_feature = processor_info.eax;
        println!("Intel platform with highest function {}", highest_feature);
    }

    let intel_hybrid_platform = power_x86_64::is_intel_hybrid_platform()?;

    //If system is not Intel platform, hybrid should be false.
    if !intel_platform {
        assert!(!intel_hybrid_platform);
    }

    //If system is Intel platform but if the highest function is less than 7
    //hybrid should be false.
    if intel_platform && highest_feature < CPUID_EAX_EXT_FEATURE {
        assert!(!intel_hybrid_platform);
    }

    println!(
        "Does platform support Intel hybrid feature? {}",
        intel_hybrid_platform
    );

    Ok(())
}

#[test]
#[cfg(target_arch = "x86_64")]
fn test_power_platform_feature_media_dynamic_cgroup_enabled() -> Result<()> {
    let platform_media_dynamic_cgroup =
        power_x86_64::platform_feature_media_dynamic_cgroup_enabled(&PathBuf::from("/"));

    assert!(platform_media_dynamic_cgroup.is_ok());

    println!(
        "Does platform support media dynamic cgroup? {}",
        platform_media_dynamic_cgroup.unwrap()
    );

    Ok(())
}

#[test]
#[cfg(target_arch = "x86_64")]
fn test_power_get_intel_hybrid_core_num() -> Result<()> {
    let dir = TempDir::new().unwrap();

    // Create fake sysfs ../cpufreq/policy*/cpuinfo_max_freq.
    // Start with platform with 4 ISO cores.
    for cpu in 0..4 {
        let mut cpu_freq_info = dir.path().to_owned();

        // Create fake sysfs ../cpufreq/policy*/ directory.
        cpu_freq_info
            .push(String::from("sys/devices/system/cpu/cpufreq/policy") + &cpu.to_string() + "/");
        std::fs::create_dir_all(&cpu_freq_info).unwrap();

        // Create fake sysfs ../cpufreq/policy*/cpufino_max_freq.
        std::fs::write(cpu_freq_info.join("cpuinfo_max_freq"), "6000")?;
    }

    // Check (total_core_num, total_ecore_num).
    let core_num = power_x86_64::get_intel_hybrid_core_num(dir.path()).unwrap();
    assert_eq!(core_num, (4, 0));

    // Add fake 8 e-cores sysfs.
    for cpu in 4..12 {
        let mut cpu_freq_info = dir.path().to_owned();

        // Create fake sysfs ../cpufreq/policy*/ directory.
        cpu_freq_info
            .push(String::from("sys/devices/system/cpu/cpufreq/policy") + &cpu.to_string() + "/");
        std::fs::create_dir_all(&cpu_freq_info).unwrap();

        // Create fake sysfs ../cpufreq/policy*/cpufino_max_freq.
        std::fs::write(cpu_freq_info.join("cpuinfo_max_freq"), "4000")?;
    }

    // Check (total_core_num, total_ecore_num).
    let core_num = power_x86_64::get_intel_hybrid_core_num(dir.path()).unwrap();
    assert_eq!(core_num, (12, 8));

    Ok(())
}
