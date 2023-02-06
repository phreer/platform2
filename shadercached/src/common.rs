// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Result};
use libchromeos::sys::warn;
use regex::Regex;
use std::{
    collections::HashMap,
    process::{Command, Stdio},
    time::Duration,
};

pub type SteamAppId = u64;

// GPU device id reported by the pcie ID
lazy_static! {
    pub static ref GPU_DEVICE_ID: u16 = get_gpu_device_id().unwrap_or(0);
    pub static ref GPU_DEVICE_DLC_VARIANT: &'static str = {
        const DLC_VARIANT_AXE: &str = "-axe";
        const DLC_VARIANT_BATRIDER: &str = "-batrider";
        const DLC_VARIANT_CLINKZ: &str = "-clinkz";

        let variant_mapping: HashMap<u16, &str> = HashMap::from([
            // axe variant
            (u16::from_str_radix("9a49", 16).unwrap(), DLC_VARIANT_AXE),  // volteer
            (u16::from_str_radix("46a6", 16).unwrap(), DLC_VARIANT_AXE),  // brya

            // batrider variant
            (u16::from_str_radix("9a40", 16).unwrap(), DLC_VARIANT_BATRIDER),  // volteer
            (u16::from_str_radix("46b3", 16).unwrap(), DLC_VARIANT_BATRIDER),  // brya

            // clinkz variant
            (u16::from_str_radix("9a78", 16).unwrap(), DLC_VARIANT_CLINKZ),  // volteer
        ]);

        // If no device id is detected or not found in |variant_mapping|,
        // shadercached should attempt to install variant-less DLC (ie. no
        // DLC suffix)
        variant_mapping.get(&*GPU_DEVICE_ID).unwrap_or(&"")
    };
}

pub const UNMOUNTER_INTERVAL: Duration = Duration::from_millis(1000);

pub fn steam_app_id_to_dlc(steam_app_id: SteamAppId) -> String {
    format!(
        "borealis-shader-cache-{}-dlc{}",
        steam_app_id, *GPU_DEVICE_DLC_VARIANT
    )
}

pub fn dlc_to_steam_app_id(dlc_name: &str) -> Result<SteamAppId> {
    lazy_static! {
        static ref RE: Regex = Regex::new(r"borealis-shader-cache-([0-9]+)-dlc(-.+)?").unwrap();
    }
    if let Some(capture) = RE.captures(dlc_name) {
        if let Some(steam_app_id_match) = capture.get(1) {
            return steam_app_id_match
                .as_str()
                .parse::<SteamAppId>()
                .map_err(|e| anyhow!(e));
        }
    }
    Err(anyhow!("Not a valid DLC"))
}

fn get_gpu_device_id() -> Result<u16> {
    // This function is called only once to initialize pub lazy static constant
    // GPU_DEVICE_ID, so we don't need to make the Regex object static.
    let regex = Regex::new(r"\[([0-9a-f]{4})\]").unwrap();
    let output = Command::new("lspci")
        .args(["-nn", "-d", "::0300", "-mm"]) // -d ::0300 returns only VGA device
        .stdout(Stdio::piped())
        .output()?;
    let vga_pcie_info = String::from_utf8(output.stdout)?;

    // Match the regex pattern for all occurrences
    let mut all_captures = regex.captures_iter(&vga_pcie_info);
    // Get the 3rd match, which has the GPU PCIE device ID
    if let Some(capture) = all_captures.nth(2) {
        // For the capture (ex. [abcd]), get the first inner match
        if let Some(device_id_match) = capture.get(1) {
            if let Ok(id) = u16::from_str_radix(device_id_match.as_str(), 16) {
                return Ok(id);
            } else {
                warn!("Failed to parse device ID: {}", device_id_match.as_str());
            }
        } else {
            warn!("Unable to extract PCI device ID, {}", vga_pcie_info);
        }
    } else {
        warn!("Unexpected VGA PCI information, {}", vga_pcie_info);
    }

    Err(anyhow!("Unable to determine PCI device ID!"))
}

#[cfg(test)]
mod tests {
    #[test]
    fn test_steam_app_id_to_dlc() {
        assert_eq!(
            super::steam_app_id_to_dlc(32),
            "borealis-shader-cache-32-dlc"
        );
        assert_eq!(
            super::steam_app_id_to_dlc(123),
            "borealis-shader-cache-123-dlc"
        );
        assert_eq!(
            super::steam_app_id_to_dlc(0000),
            "borealis-shader-cache-0-dlc"
        );
    }

    #[test]
    fn test_dlc_to_steam_app_id() {
        assert_eq!(
            super::dlc_to_steam_app_id("borealis-shader-cache-32-dlc").unwrap(),
            32
        );
        assert_eq!(
            super::dlc_to_steam_app_id("borealis-shader-cache-000-dlc").unwrap(),
            0
        );
        assert!(super::dlc_to_steam_app_id("borealis-shader-cache-213").is_err());
        assert!(super::dlc_to_steam_app_id("213-dlc").is_err());
        assert!(super::dlc_to_steam_app_id("not-a-valid-one").is_err());
        assert!(super::dlc_to_steam_app_id("borealis-dlc").is_err());
        assert!(super::dlc_to_steam_app_id("borealis-shader-cache-two-dlc").is_err());
    }
}
