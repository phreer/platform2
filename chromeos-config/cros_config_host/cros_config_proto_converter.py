#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Transforms config from /config/proto/api proto format to platform JSON."""

import argparse
import json
import sys

from collections import namedtuple

from config.api import config_bundle_pb2
from config.api import device_brand_pb2

Config = namedtuple('Config',
                    ['program',
                     'hw_design',
                     'odm',
                     'hw_design_config',
                     'device_brand',
                     'oem',
                     'sw_config',
                     'brand_config',
                     'build_target'])


def ParseArgs(argv):
  """Parse the available arguments.

  Invalid arguments or -h cause this function to print a message and exit.

  Args:
    argv: List of string arguments (excluding program name / argv[0])

  Returns:
    argparse.Namespace object containing the attributes.
  """
  parser = argparse.ArgumentParser(
      description='Converts source proto config into platform JSON config.')
  parser.add_argument(
      '-c',
      '--project_config',
      type=str,
      help='Path to the source project-level protobinary file')
  parser.add_argument(
      '-p',
      '--program_config',
      type=str,
      help='Path to the source program-level protobinary file')
  parser.add_argument(
      '-o',
      '--output',
      type=str,
      help='Output file that will be generated')
  return parser.parse_args(argv)


def _Set(field, target, target_name):
  if field:
    target[target_name] = field


def _BuildArc(config):
  if config.build_target.arc:
    return {
        'build-properties': {
            'device': config.build_target.arc.device,
            'first-api-level': config.build_target.arc.first_api_level,
            'marketing-name': config.device_brand.brand_name,
            'oem': config.oem.name if config.oem else None,
            'metrics-tag': config.hw_design.name,
            'product': config.hw_design.name,
        }
    }


def _FwBcsPath(payload):
  if payload:
    return 'bcs://%s.%d.%d.0.tbz2' % (
        payload.firmware_image_name,
        payload.version.major,
        payload.version.minor)


def _FwBuildTarget(payload):
  if payload:
    return payload.build_target_name


def _BuildFirmware(config):
  if not config.sw_config.firmware:
    return {
        'no-firmware': True,
    }
  fw = config.sw_config.firmware
  main_ro = fw.main_ro_payload
  main_rw = fw.main_rw_payload
  ec_ro = fw.ec_ro_payload
  pd_ro = fw.pd_ro_payload

  result = {
      'bcs-overlay': config.build_target.overlay_name,
      'build-targets': {
          'coreboot': _FwBuildTarget(main_rw),
          'depthcharge': _FwBuildTarget(main_ro),
          'ec': _FwBuildTarget(ec_ro),
          # Convert to list from proto iterator for string lists
          'ec_extras': list(fw.ec_extras),
          'libpayload': _FwBuildTarget(pd_ro),
      },
      # TODO(shapiroc): Resolve this with jettrink@ (where it's sourced)
      # I think this is wrong here based on previous discussions.
      # 'firmware-config': 0,
      'image-name': main_ro.firmware_image_name,
  }
  _Set(_FwBcsPath(fw.main_ro_payload), result, 'main-ro-image')
  _Set(_FwBcsPath(fw.main_rw_payload), result, 'main-rw-image')
  _Set(_FwBcsPath(fw.ec_ro_payload), result, 'ec-ro-image')
  _Set(_FwBcsPath(fw.pd_ro_payload), result, 'pd-ro-image')

  return result


def _BuildFwSigning(config):
  if not config.sw_config.firmware:
    return {}
  # TODO(shapiroc): Source signing config from separate private repo
  return {
      'key-id': 'DEFAULT',
      'signature-id': config.hw_design.name,
  }


def _File(source, destination):
  return {
      'destination': destination,
      'source': source
  }


def _BuildAudio(config):
  alsa_path = '/usr/share/alsa/ucm'
  cras_path = '/etc/cras'
  project_name = config.hw_design.name
  # File that matches the cardname when installed and points to HiFi.conf
  # TODO(shapiroc): Plumb the defaults in chromeos-bsp files structure
  card_name_file = 'audio-defaults/card-name-file.conf'
  if not config.sw_config.audio_config:
    return {}
  audio = config.sw_config.audio_config
  card = audio.card_name
  files = []
  if audio.ucm_file:
    files.append(_File(audio.ucm_file, '%s/%s/HiFi.conf' % (alsa_path, card)))
    files.append(_File(
        card_name_file, '%s/%s/%s.conf' % (alsa_path, card, card)))
  if audio.card_config_file:
    files.append(_File(
        audio.card_config_file, '%s/%s/%s' % (cras_path, project_name, card)))
  if audio.dsp_file:
    files.append(
        _File(audio.ucm_file, '%s/%s/dsp.ini' % (cras_path, project_name)))
  return {
      'main': {
          'cras-config-dir': project_name,
          'files': files,
      }
  }


def _BuildIdentity(hw_scan_config, brand_scan_config=None):
  identity = {}
  _Set(hw_scan_config.firmware_sku, identity, 'sku-id')
  _Set(hw_scan_config.smbios_name_match, identity, 'smbios-name-match')
  # Platform name is a redundant relic of mosys
  _Set(hw_scan_config.smbios_name_match, identity, 'platform-name')
  # ARM architecture
  _Set(hw_scan_config.device_tree_compatible_match, identity,
       'device-tree-compatible-match')

  if brand_scan_config:
    _Set(brand_scan_config.whitelabel_tag, identity, 'whitelabel-tag')

  return identity


def _Lookup(id_value, id_map):
  return id_map[id_value.value] if id_value else None


def _TransformBuildConfigs(config):
  partners = dict([(x.id.value, x) for x in config.partners.value])
  programs = dict([(x.id.value, x) for x in config.programs.value])
  build_targets = dict([(x.id.value, x) for x in config.build_targets])
  sw_configs = dict([(x.id.value, x) for x in config.software_configs])
  brand_configs = dict([(x.brand_id.value, x) for x in config.brand_configs])

  results = []
  for hw_design in config.designs.value:
    device_brands = (x for x in config.device_brands.value
                     if x.design_id.value == hw_design.id.value)
    if not device_brands:
      device_brands = [device_brand_pb2.DeviceBrand()]

    for device_brand in device_brands:
      for hw_design_config in hw_design.configs:
        config = Config(
            program=_Lookup(hw_design.program_id, programs),
            hw_design=hw_design,
            odm=_Lookup(hw_design.odm_id, partners),
            hw_design_config=hw_design_config,
            device_brand=device_brand,
            oem=_Lookup(device_brand.oem_id, partners),
            sw_config=_Lookup(hw_design_config.software_config_id, sw_configs),
            brand_config=_Lookup(device_brand.id, brand_configs),
            build_target=_Lookup(hw_design.build_target_id, build_targets))
        results.append(_TransformBuildConfig(config))

  return results


def _TransformBuildConfig(config):
  """Transforms Config instance into target platform JSON schema.

  Args:
    config: Config namedtuple

  Returns:
    Unique config payload based on the platform JSON schema.
  """
  result = {
      'identity': _BuildIdentity(
          config.sw_config.scan_config,
          config.brand_config.scan_config),
  }

  _Set(_BuildArc(config), result, 'arc')
  _Set(_BuildAudio(config), result, 'audio')
  _Set(config.device_brand.brand_code, result, 'brand-code')
  _Set(_BuildFirmware(config), result, 'firmware')
  _Set(_BuildFwSigning(config), result, 'firmware-signing')

  return result


def WriteOutput(configs, output=None):
  """Writes a list of configs to platform JSON format.

  Args:
    configs: List of config dicts defined in cros_config_schema.yaml
    output: Target file output (if None, prints to stdout)
  """
  json_output = json.dumps(
      {'chromeos': {
          'configs': configs,
      }},
      sort_keys=True,
      indent=2,
      separators=(',', ': '))
  if output:
    with open(output, 'w') as output_stream:
      # Using print function adds proper trailing newline.
      print(json_output, file=output_stream)
  else:
    print(json_output)


def _ReadConfig(path):
  """Reads a binary proto from a file.

  Args:
    path: Path to the binary proto.
  """
  config = config_bundle_pb2.ConfigBundle()
  with open(path, 'rb') as f:
    config.ParseFromString(f.read())
  return config


def _MergeConfigs(configs):
  result = config_bundle_pb2.ConfigBundle()
  for config in configs:
    result.MergeFrom(config)

  return result


def Main(project_config,
         program_config,
         output):
  """Transforms source proto config into platform JSON.

  Args:
    project_config: Source project config.
    program_config: Source program config.
    output: Output file that will be generated by the transform.
  """
  WriteOutput(
      _TransformBuildConfigs(
          _MergeConfigs(
              [_ReadConfig(project_config),
               _ReadConfig(program_config)]),),
      output)


def main(argv=None):
  """Main program which parses args and runs

  Args:
    argv: List of command line arguments, if None uses sys.argv.
  """
  if argv is None:
    argv = sys.argv[1:]
  opts = ParseArgs(argv)
  Main(opts.project_config, opts.program_config, opts.output)


if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))
