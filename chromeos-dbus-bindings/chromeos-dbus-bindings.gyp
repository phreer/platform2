{
  'target_defaults': {
    'variables': {
      'deps': [
        'libchrome-<(libbase_ver)',
        'libchromeos-<(libbase_ver)',
      ],
    },
    'cflags': [
      '-Wextra',
      '-Wno-unused-parameter',  # for scoped_ptr.h, included indirectly
    ],
    'cflags_cc': [
      '-fno-strict-aliasing',
      '-Woverloaded-virtual',
    ],
    'defines': [
      '__STDC_FORMAT_MACROS',
      '__STDC_LIMIT_MACROS',
    ],
  },
  'targets': [
    {
      'target_name': 'libchromeos-dbus-bindings',
      'type': 'static_library',
      'sources': [
        'dbus_signature.cc',
        'method_name_generator.cc',
        'xml_interface_parser.cc',
      ],
      'variables': {
        'exported_deps': [
          'expat',
        ],
        'deps': [
          'dbus-1',
          '<@(exported_deps)',
        ],
      },
      'all_dependent_settings': {
        'variables': {
          'deps': [
            '<@(exported_deps)',
          ],
        },
      },
      'link_settings': {
        'variables': {
          'deps': [
            'expat',
          ],
        },
      },
    },
    {
      'target_name': 'generate-chromeos-dbus-bindings',
      'type': 'executable',
      'dependencies': ['libchromeos-dbus-bindings'],
      'sources': [
        'generate_chromeos_dbus_bindings.cc',
      ]
    },
  ],
  'conditions': [
    ['USE_test == 1', {
      'targets': [
        {
          'target_name': 'chromeos_dbus_bindings_unittest',
          'type': 'executable',
          'dependencies': ['libchromeos-dbus-bindings'],
          'includes': ['../common-mk/common_test.gypi'],
          'sources': [
            'testrunner.cc',
            'dbus_signature_unittest.cc',
            'method_name_generator_unittest.cc',
            'xml_interface_parser_unittest.cc',
          ],
        },
      ],
    }],
  ],
}
