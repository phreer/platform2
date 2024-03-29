# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

project('ime_bridge', 'c', 'cpp',
        default_options : ['cpp_std=c++17'])

#=========#
# Wayland #
#=========#

wl_scanner = find_program('wayland-scanner')

wl_generators = [
  generator(
    wl_scanner,
    output: '@BASENAME@-code.c',
    arguments: ['private-code', '@INPUT@', '@OUTPUT@']
  ),
  generator(
    wl_scanner,
    output: '@BASENAME@-client-protocol.h',
    arguments: ['client-header', '@INPUT@', '@OUTPUT@']
  ),
]

wl_protocols = [
  'text-input-extension-unstable-v1.xml',
  'text-input-unstable-v1.xml',
]

wl_outs = []

foreach p : wl_protocols
  foreach g : wl_generators
    wl_outs += g.process(get_option('wl_protocol_dir') / p)
  endforeach
endforeach

#============#
# ime_bridge #
#============#

backend_sources = [
  'backend/im_context_backend.cc',
  'backend/im_context_backend.h',
  'backend/text_input.h',
  'backend/text_input_enums.h',
  'backend/wayland_client.h',
  'backend/wayland_manager.cc',
  'backend/wayland_manager.h',
]

gtk_sources = backend_sources + [
  'frontend/gtk/cros_gtk_im_context.cc',
  'frontend/gtk/cros_gtk_im_context.h',
  'frontend/gtk/im_cros.cc',
]

libim_cros_gtk = shared_library('im_cros_gtk',
  sources: gtk_sources + wl_outs,
  dependencies: [
    meson.get_compiler('cpp').find_library('m'),
    dependency('gtk+-3.0'),
    dependency('wayland-client'),
  ],
)

libim_test_cros_gtk = shared_library('im_test_cros_gtk',
  sources: [
    'backend/test/backend_test.cc',
    'backend/test/backend_test.h',
    'backend/test/backend_test_utils.h',
    'backend/test/event.cc',
    'backend/test/event.h',
    'backend/test/gtk_commit_string_test_backend.cc',
    'backend/test/gtk_content_type_test_backend.cc',
    'backend/test/gtk_key_sym_test_backend.cc',
    'backend/test/gtk_set_preedit_region_test_backend.cc',
    'backend/test/gtk_windowing_test_backend.cc',
    'backend/test/mock_text_input.cc',
    'backend/test/mock_text_input.h',
    'backend/test/mock_wayland_client.cc',
    'backend/test/mock_wayland_client.h',
    'backend/test/request.cc',
    'backend/test/request.h',
  ] + gtk_sources,
  dependencies: [
    meson.get_compiler('cpp').find_library('m'),
    dependency('gtk+-3.0'),
  ],
  cpp_args: '-DTEST_BACKEND',
)

test_sources = [
  'test/gtk_commit_string_test.cc',
  'test/gtk_content_type_test.cc',
  'test/gtk_key_sym_test.cc',
  'test/gtk_set_preedit_region_test.cc',
  'test/gtk_test_base.h',
  'test/gtk_windowing_test.cc',
]

executable('cros_im_tests',
  sources: test_sources,
  dependencies: [
    meson.get_compiler('cpp').find_library('m'),
    dependency('gtkmm-3.0'),
    dependency('gtest', main: true),
  ],
)
