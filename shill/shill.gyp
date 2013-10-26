{
  'target_defaults': {
    'dependencies': [
      '../libchromeos/libchromeos-<(libbase_ver).gyp:libchromeos-<(libbase_ver)',
    ],
    'variables': {
      'deps': [
        'libchrome-<(libbase_ver)',
      ],
    },
    'cflags': [
      '-Wno-missing-field-initializers',
      '-Wno-unused-parameter',
      '-Wno-unused-result',
      '-Wuninitialized',
    ],
    'cflags_cc': [
      '-fno-strict-aliasing',
      '-std=gnu++11',
      '-Woverloaded-virtual',
    ],
    'defines': [
      '__STDC_FORMAT_MACROS',
      '__STDC_LIMIT_MACROS',
      'RUNDIR="/var/run/shill"',
      'SHIMDIR="<(libdir)/shill/shims"',
    ],
    'conditions': [
      ['USE_cellular == 0', {
        'defines': [
          'DISABLE_CELLULAR',
        ],
      }],
      ['USE_vpn == 0', {
        'defines': [
          'DISABLE_VPN',
        ],
      }],
    ],
  },
  'targets': [
    {
      'target_name': 'shill-proxies',
      'type': 'none',
      'variables': {
        'xml2cpp_type': 'proxy',
        'xml2cpp_in_dir': 'dbus_bindings',
        'xml2cpp_out_dir': 'include/shill/dbus_proxies',
      },
      'sources': [
        '<(xml2cpp_in_dir)/dbus-properties.xml',
        '<(xml2cpp_in_dir)/dbus-service.xml',
        '<(xml2cpp_in_dir)/dhcpcd.xml',
        '<(xml2cpp_in_dir)/power_manager.xml',
        '<(xml2cpp_in_dir)/supplicant-bss.xml',
        '<(xml2cpp_in_dir)/supplicant-interface.xml',
        '<(xml2cpp_in_dir)/supplicant-network.xml',
        '<(xml2cpp_in_dir)/supplicant-process.xml',
        '<(xml2cpp_in_dir)/org.chromium.flimflam.Device.xml',
        '<(xml2cpp_in_dir)/org.chromium.flimflam.IPConfig.xml',
        '<(xml2cpp_in_dir)/org.chromium.flimflam.Manager.xml',
        '<(xml2cpp_in_dir)/org.chromium.flimflam.Profile.xml',
        '<(xml2cpp_in_dir)/org.chromium.flimflam.Service.xml',
        '<(xml2cpp_in_dir)/org.chromium.flimflam.Task.xml',
      ],
      'conditions': [
        ['USE_cellular == 1', {
          'sources': [
            '<(xml2cpp_in_dir)/dbus-objectmanager.xml',
            '<(xml2cpp_in_dir)/modem-gobi.xml',
          ],
        }],
      ],
      'includes': ['../common-mk/xml2cpp.gypi'],
    },
    {
      'target_name': 'shill-adaptors',
      'type': 'none',
      'variables': {
        'xml2cpp_type': 'adaptor',
        'xml2cpp_in_dir': 'dbus_bindings',
        'xml2cpp_out_dir': 'include/shill/dbus_adaptors',
      },
      'sources': [
        '<(xml2cpp_in_dir)/org.chromium.flimflam.Device.xml',
        '<(xml2cpp_in_dir)/org.chromium.flimflam.IPConfig.xml',
        '<(xml2cpp_in_dir)/org.chromium.flimflam.Manager.xml',
        '<(xml2cpp_in_dir)/org.chromium.flimflam.Profile.xml',
        '<(xml2cpp_in_dir)/org.chromium.flimflam.Service.xml',
        '<(xml2cpp_in_dir)/org.chromium.flimflam.Task.xml',
      ],
      'includes': ['../common-mk/xml2cpp.gypi'],
    },
    {
      'target_name': 'shim-protos',
      'type': 'static_library',
      'variables': {
        'proto_in_dir': 'shims/protos',
        'proto_out_dir': 'include/shill/proto_bindings/shims/protos',
      },
      'sources': [
        '<(proto_in_dir)/crypto_util.proto',
      ],
      'includes': ['../common-mk/protoc.gypi'],
    },
    {
      'target_name': 'crypto_util',
      'type': 'executable',
      'dependencies': ['shim-protos'],
      'sources': [
        'shims/crypto_util.cc',
      ]
    },
    {
      'target_name': 'libshill',
      'type': 'static_library',
      'dependencies': [
        '../common-mk/external_dependencies.gyp:modemmanager-dbus-proxies',
        '../metrics/metrics.gyp:libmetrics',
        '../system_api/system_api.gyp:system_api-power_manager-protos',
        '../wimax_manager/wimax_manager.gyp:wimax_manager-proxies',
        'shill-adaptors',
        'shill-proxies',
        'shim-protos',
      ],
      'variables': {
        'exported_deps': [
          'dbus-c++-1',
          'gio-2.0',
          'glib-2.0',
          'libcares',
          'libnl-3.0',
          'libnl-genl-3.0',
          'protobuf-lite',
        ],
        'deps': ['<@(exported_deps)'],
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
            'dbus-c++-1',
            'gio-2.0',
            'glib-2.0',
            'libcares',
            'libnl-3.0',
            'libnl-genl-3.0',
            'protobuf-lite',
          ],
        },
        'libraries': [
          '-lbootstat',
          '-lminijail',
          '-lrt'
        ],
      },
      'conditions': [
        ['USE_cellular == 1', {
          'link_settings': {
            'libraries': [
              '-lmobile-provider'
            ],
            'variables': {
              'deps': [
                'ModemManager',
              ],
            },
            'sources': [
              'cellular.cc',
              'cellular_capability.cc',
              'cellular_capability_cdma.cc',
              'cellular_capability_classic.cc',
              'cellular_capability_gsm.cc',
              'cellular_capability_universal.cc',
              'cellular_capability_universal_cdma.cc',
              'cellular_error.cc',
              'cellular_error_mm1.cc',
              'cellular_operator_info.cc',
              'cellular_service.cc',
              'dbus_objectmanager_proxy.cc',
              'mm1_bearer_proxy.cc',
              'mm1_modem_location_proxy.cc',
              'mm1_modem_modem3gpp_proxy.cc',
              'mm1_modem_modemcdma_proxy.cc',
              'mm1_modem_proxy.cc',
              'mm1_modem_simple_proxy.cc',
              'mm1_modem_time_proxy.cc',
              'mm1_sim_proxy.cc',
              'modem.cc',
              'modem_1.cc',
              'modem_cdma_proxy.cc',
              'modem_classic.cc',
              'modem_gobi_proxy.cc',
              'modem_gsm_card_proxy.cc',
              'modem_gsm_network_proxy.cc',
              'modem_manager.cc',
              'modem_manager_1.cc',
              'modem_manager_proxy.cc',
              'modem_proxy.cc',
              'modem_simple_proxy.cc',
            ],
          },
        }],
        ['USE_vpn == 1', {
          'link_settings': {
            'libraries': [
            ],
            'variables': {
              'deps': [
              ],
            },
            'sources': [
              'l2tp_ipsec_driver.cc',
              'openvpn_driver.cc',
              'openvpn_management_server.cc',
            ],
          },
        }],
      ],
      'sources': [
        'arp_client.cc',
        'arp_packet.cc',
        'async_connection.cc',
        'attribute_list.cc',
        'byte_string.cc',
        'callback80211_metrics.cc',
        'certificate_file.cc',
        'connection.cc',
        'connection_health_checker.cc',
        'connection_info.cc',
        'connection_info_reader.cc',
        'control_netlink_attribute.cc',
        'crypto_des_cbc.cc',
        'crypto_provider.cc',
        'crypto_rot47.cc',
        'crypto_util_proxy.cc',
        'dbus_adaptor.cc',
        'dbus_control.cc',
        'dbus_manager.cc',
        'dbus_properties.cc',
        'dbus_properties_proxy.cc',
        'dbus_service_proxy.cc',
        'default_profile.cc',
        'device.cc',
        'device_dbus_adaptor.cc',
        'device_info.cc',
        'dhcp_config.cc',
        'dhcp_provider.cc',
        'dhcpcd_proxy.cc',
        'diagnostics_reporter.cc',
        'dns_client.cc',
        'dns_client_factory.cc',
        'eap_credentials.cc',
        'eap_listener.cc',
        'endpoint.cc',
        'ephemeral_profile.cc',
        'error.cc',
        'ethernet.cc',
        'ethernet_eap_provider.cc',
        'ethernet_eap_service.cc',
        'ethernet_service.cc',
        'event_dispatcher.cc',
        'external_task.cc',
        'file_io.cc',
        'file_reader.cc',
        'generic_netlink_message.cc',
        'geolocation_info.cc',
        'glib.cc',
        'glib_io_input_handler.cc',
        'glib_io_ready_handler.cc',
        'hook_table.cc',
        'http_proxy.cc',
        'http_request.cc',
        'http_url.cc',
        'ip_address.cc',
        'ip_address_store.cc',
        'ipconfig.cc',
        'ipconfig_dbus_adaptor.cc',
        'key_file_store.cc',
        'key_value_store.cc',
        'link_monitor.cc',
        'manager.cc',
        'manager_dbus_adaptor.cc',
        'memory_log.cc',
        'metrics.cc',
        'minijail.cc',
        'modem_info.cc',
        'netlink_attribute.cc',
        'netlink_manager.cc',
        'netlink_message.cc',
        'netlink_socket.cc',
        'nl80211_attribute.cc',
        'nl80211_message.cc',
        'nss.cc',
        'pending_activation_store.cc',
        'portal_detector.cc',
        'power_manager.cc',
        'power_manager_proxy.cc',
        'ppp_device.cc',
        'ppp_device_factory.cc',
        'process_killer.cc',
        'profile.cc',
        'profile_dbus_adaptor.cc',
        'profile_dbus_property_exporter.cc',
        'property_store.cc',
        'proxy_factory.cc',
        'resolver.cc',
        'result_aggregator.cc',
        'routing_table.cc',
        'rpc_task.cc',
        'rpc_task_dbus_adaptor.cc',
        'rtnl_handler.cc',
        'rtnl_listener.cc',
        'rtnl_message.cc',
        'scan_session.cc',
        'scope_logger.cc',
        'service.cc',
        'service_dbus_adaptor.cc',
        'shill_ares.cc',
        'shill_config.cc',
        'shill_daemon.cc',
        'shill_test_config.cc',
        'shill_time.cc',
        'socket_info.cc',
        'socket_info_reader.cc',
        'sockets.cc',
        'static_ip_parameters.cc',
        'supplicant_bss_proxy.cc',
        'supplicant_eap_state_handler.cc',
        'supplicant_interface_proxy.cc',
        'supplicant_network_proxy.cc',
        'supplicant_process_proxy.cc',
        'technology.cc',
        'traffic_monitor.cc',
        'virtio_ethernet.cc',
        'virtual_device.cc',
        'vpn_driver.cc',
        'vpn_provider.cc',
        'vpn_service.cc',
        'wifi.cc',
        'wifi_endpoint.cc',
        'wifi_provider.cc',
        'wifi_service.cc',
        'wimax.cc',
        'wimax_device_proxy.cc',
        'wimax_manager_proxy.cc',
        'wimax_network_proxy.cc',
        'wimax_provider.cc',
        'wimax_service.cc',
        'wpa_supplicant.cc',
      ],
    },
    {
      'target_name': 'shill',
      'type': 'executable',
      'dependencies': ['libshill'],
      'sources': [
        'shill_main.cc',
      ]
    },
    {
      'target_name': 'shill-pppd-plugin',
      'type': 'shared_library',
      'dependencies': ['shill-proxies'],
      'sources': [
        'shims/c_ppp.cc',
        'shims/environment.cc',
        'shims/ppp.cc',
        'shims/pppd_plugin.c',
        'shims/task_proxy.cc',
      ]
    },
    {
      'target_name': 'crypto-util',
      'type': 'executable',
      'dependencies': ['shim-protos'],
      'variables': {
        'deps': [
          'openssl',
          'protobuf-lite',
        ],
      },
      'sources': [
        'shims/crypto_util.cc',
      ]
    },
    {
      'target_name': 'net-diags-upload',
      'type': 'executable',
      'sources': [
        'shims/net_diags_upload.cc',
      ]
    },
    {
      'target_name': 'nss-get-cert',
      'type': 'executable',
      'dependencies': ['libshill'],
      'variables': {
        'deps': [
          'nss',
        ],
      },
      'sources': [
        'shims/certificates.cc',
        'shims/nss_get_cert.cc',
      ]
    },
    {
      'target_name': 'netfilter-queue-helper',
      'type': 'executable',
      'variables': {
        'deps': [
          'libmnl',
          'libnetfilter_queue',
          'libnfnetlink',
        ],
      },
      'sources': [
        'shims/netfilter_queue_helper.cc',
        'shims/netfilter_queue_processor.cc',
      ]
    },
    {
      'target_name': 'openvpn-script',
      'type': 'executable',
      'dependencies': ['shill-proxies'],
      'variables': {
        'deps': [
          'dbus-c++-1',
        ],
      },
      'sources': [
        'shims/environment.cc',
        'shims/openvpn_script.cc',
        'shims/task_proxy.cc',
      ]
    },
  ],
  'conditions': [
    ['USE_cellular == 1', {
      'targets': [
        {
          'target_name': 'set-apn-helper',
          'type': 'executable',
          'sources': [
            'shims/set_apn_helper.c',
          ]
        },
      ],
    }],
    ['USE_test == 1', {
      'targets': [
        {
          'target_name': 'shill_unittest',
          'type': 'executable',
          'dependencies': ['libshill'],
          'includes': ['../common-mk/common_test.gypi'],
          'variables': {
            'deps': [
              'nss',
              'libmnl',
              'libnetfilter_queue',
              'libnfnetlink',
            ],
          },
          'defines': [
            'SYSROOT="<(sysroot)"',
          ],
          'sources': [
            'arp_client_unittest.cc',
            'arp_packet_unittest.cc',
            'async_connection_unittest.cc',
            'byte_string_unittest.cc',
            'callback80211_metrics_unittest.cc',
            'certificate_file_unittest.cc',
            'connection_health_checker_unittest.cc',
            'connection_info_reader_unittest.cc',
            'connection_info_unittest.cc',
            'connection_unittest.cc',
            'crypto_des_cbc_unittest.cc',
            'crypto_provider_unittest.cc',
            'crypto_rot47_unittest.cc',
            'crypto_util_proxy_unittest.cc',
            'dbus_adaptor_unittest.cc',
            'dbus_manager_unittest.cc',
            'dbus_properties_unittest.cc',
            'dbus_variant_gmock_printer.cc',
            'default_profile_unittest.cc',
            'device_info_unittest.cc',
            'device_unittest.cc',
            'dhcp_config_unittest.cc',
            'dhcp_provider_unittest.cc',
            'diagnostics_reporter_unittest.cc',
            'dns_client_unittest.cc',
            'eap_credentials_unittest.cc',
            'eap_listener_unittest.cc',
            'error_unittest.cc',
            'ethernet_eap_provider_unittest.cc',
            'ethernet_eap_service_unittest.cc',
            'ethernet_service_unittest.cc',
            'ethernet_unittest.cc',
            'external_task_unittest.cc',
            'file_reader_unittest.cc',
            'hook_table_unittest.cc',
            'http_proxy_unittest.cc',
            'http_request_unittest.cc',
            'http_url_unittest.cc',
            'ip_address_store_unittest.cc',
            'ip_address_unittest.cc',
            'ipconfig_unittest.cc',
            'key_file_store_unittest.cc',
            'key_value_store_unittest.cc',
            'link_monitor_unittest.cc',
            'manager_unittest.cc',
            'memory_log_unittest.cc',
            'metrics_unittest.cc',
            'mock_adaptors.cc',
            'mock_ares.cc',
            'mock_arp_client.cc',
            'mock_async_connection.cc',
            'mock_certificate_file.cc',
            'mock_connection.cc',
            'mock_connection_health_checker.cc',
            'mock_connection_info_reader.cc',
            'mock_control.cc',
            'mock_crypto_util_proxy.cc',
            'mock_dbus_manager.cc',
            'mock_dbus_objectmanager_proxy.cc',
            'mock_dbus_properties_proxy.cc',
            'mock_dbus_service_proxy.cc',
            'mock_device.cc',
            'mock_device_info.cc',
            'mock_dhcp_config.cc',
            'mock_dhcp_provider.cc',
            'mock_dhcp_proxy.cc',
            'mock_diagnostics_reporter.cc',
            'mock_dns_client.cc',
            'mock_dns_client_factory.cc',
            'mock_eap_credentials.cc',
            'mock_eap_listener.cc',
            'mock_ethernet.cc',
            'mock_ethernet_eap_provider.cc',
            'mock_ethernet_service.cc',
            'mock_event_dispatcher.cc',
            'mock_external_task.cc',
            'mock_glib.cc',
            'mock_http_request.cc',
            'mock_ip_address_store.cc',
            'mock_ipconfig.cc',
            'mock_link_monitor.cc',
            'mock_log.cc',
            'mock_log_unittest.cc',
            'mock_manager.cc',
            'mock_metrics.cc',
            'mock_minijail.cc',
            'mock_modem_info.cc',
            'mock_netlink_manager.cc',
            'mock_nss.cc',
            'mock_pending_activation_store.cc',
            'mock_portal_detector.cc',
            'mock_power_manager.cc',
            'mock_power_manager_proxy.cc',
            'mock_ppp_device.cc',
            'mock_ppp_device_factory.cc',
            'mock_process_killer.cc',
            'mock_profile.cc',
            'mock_property_store.cc',
            'mock_proxy_factory.cc',
            'mock_resolver.cc',
            'mock_routing_table.cc',
            'mock_rtnl_handler.cc',
            'mock_scan_session.cc',
            'mock_service.cc',
            'mock_socket_info_reader.cc',
            'mock_sockets.cc',
            'mock_store.cc',
            'mock_supplicant_bss_proxy.cc',
            'mock_supplicant_eap_state_handler.cc',
            'mock_supplicant_interface_proxy.cc',
            'mock_supplicant_network_proxy.cc',
            'mock_supplicant_process_proxy.cc',
            'mock_time.cc',
            'mock_traffic_monitor.cc',
            'mock_virtual_device.cc',
            'mock_vpn_provider.cc',
            'mock_wifi.cc',
            'mock_wifi_provider.cc',
            'mock_wifi_service.cc',
            'mock_wimax.cc',
            'mock_wimax_device_proxy.cc',
            'mock_wimax_manager_proxy.cc',
            'mock_wimax_network_proxy.cc',
            'mock_wimax_provider.cc',
            'mock_wimax_service.cc',
            'modem_info_unittest.cc',
            'netlink_manager_unittest.cc',
            'netlink_message_unittest.cc',
            'netlink_socket_unittest.cc',
            'nice_mock_control.cc',
            'nss_unittest.cc',
            'pending_activation_store_unittest.cc',
            'portal_detector_unittest.cc',
            'power_manager_unittest.cc',
            'ppp_device_unittest.cc',
            'process_killer_unittest.cc',
            'profile_dbus_property_exporter_unittest.cc',
            'profile_unittest.cc',
            'property_accessor_unittest.cc',
            'property_store_unittest.cc',
            'resolver_unittest.cc',
            'result_aggregator_unittest.cc',
            'routing_table_unittest.cc',
            'rpc_task_unittest.cc',
            'rtnl_handler_unittest.cc',
            'rtnl_listener_unittest.cc',
            'rtnl_message_unittest.cc',
            'scan_session_unittest.cc',
            'scope_logger_unittest.cc',
            'service_property_change_test.cc',
            'service_under_test.cc',
            'service_unittest.cc',
            'shill_unittest.cc',
            'shims/certificates.cc',
            'shims/certificates_unittest.cc',
            'shims/netfilter_queue_processor.cc',
            'shims/netfilter_queue_processor_unittest.cc',
            'socket_info_reader_unittest.cc',
            'socket_info_unittest.cc',
            'static_ip_parameters_unittest.cc',
            'supplicant_eap_state_handler_unittest.cc',
            'technology_unittest.cc',
            'testrunner.cc',
            'traffic_monitor_unittest.cc',
            'virtual_device_unittest.cc',
            'wifi_endpoint_unittest.cc',
            'wifi_provider_unittest.cc',
            'wifi_service_unittest.cc',
            'wifi_unittest.cc',
            'wimax_provider_unittest.cc',
            'wimax_service_unittest.cc',
            'wimax_unittest.cc',
            'wpa_supplicant_unittest.cc',
          ],
          'conditions': [
            ['USE_cellular == 1', {
              'link_settings': {
                'sources': [
                  'cellular_capability_cdma_unittest.cc',
                  'cellular_capability_classic_unittest.cc',
                  'cellular_capability_gsm_unittest.cc',
                  'cellular_capability_universal_cdma_unittest.cc',
                  'cellular_capability_universal_unittest.cc',
                  'cellular_error_unittest.cc',
                  'cellular_operator_info_unittest.cc',
                  'cellular_service_unittest.cc',
                  'cellular_unittest.cc',
                  'mock_cellular.cc',
                  'mock_cellular_operator_info.cc',
                  'mock_cellular_service.cc',
                  'mock_mm1_bearer_proxy.cc',
                  'mock_mm1_modem_location_proxy.cc',
                  'mock_mm1_modem_modem3gpp_proxy.cc',
                  'mock_mm1_modem_modemcdma_proxy.cc',
                  'mock_mm1_modem_proxy.cc',
                  'mock_mm1_modem_simple_proxy.cc',
                  'mock_mm1_modem_time_proxy.cc',
                  'mock_mm1_sim_proxy.cc',
                  'mock_modem.cc',
                  'mock_modem_cdma_proxy.cc',
                  'mock_modem_gobi_proxy.cc',
                  'mock_modem_gsm_card_proxy.cc',
                  'mock_modem_gsm_network_proxy.cc',
                  'mock_modem_manager_proxy.cc',
                  'mock_modem_proxy.cc',
                  'mock_modem_simple_proxy.cc',
                  'modem_1_unittest.cc',
                  'modem_manager_unittest.cc',
                  'modem_unittest.cc',
                ],
              },
            }],
            ['USE_vpn == 1', {
              'sources': [
                'l2tp_ipsec_driver_unittest.cc',
                'mock_openvpn_driver.cc',
                'mock_openvpn_management_server.cc',
                'mock_vpn_driver.cc',
                'mock_vpn_service.cc',
                'openvpn_driver_unittest.cc',
                'openvpn_management_server_unittest.cc',
                'shims/environment.cc',
                'shims/environment_unittest.cc',
                'shims/task_proxy.cc',
                'vpn_driver_unittest.cc',
                'vpn_provider_unittest.cc',
                'vpn_service_unittest.cc',
              ],
            }],
          ],
        },
      ],
    }],
  ],
}
