// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(
    dead_code,
    non_camel_case_types,
    non_snake_case,
    non_upper_case_globals
)]

/*
automatically generated by rust-bindgen

generated with the command:
cd ${CHROMEOS_DIR}/src/platform2/ && \
bindgen arc/vm/libvda/libvda_encode.h \
  -o arc/vm/libvda/rust/src/encode_bindings.rs \
  --whitelist-function "initialize_encode" \
  --whitelist-function "deinitialize_encode" \
  --whitelist-function "get_vea_capabilities" \
  --whitelist-function "init_encode_session" \
  --whitelist-function "close_encode_session" \
  --whitelist-function "vea_.*" \
  --whitelist-type "vea_.*" \
  -- \
  -I .
*/

pub type __uint8_t = ::std::os::raw::c_uchar;
pub type __int32_t = ::std::os::raw::c_int;
pub type __uint32_t = ::std::os::raw::c_uint;
pub type __int64_t = ::std::os::raw::c_long;
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct video_frame_plane {
    pub offset: i32,
    pub stride: i32,
}
#[test]
fn bindgen_test_layout_video_frame_plane() {
    assert_eq!(
        ::std::mem::size_of::<video_frame_plane>(),
        8usize,
        concat!("Size of: ", stringify!(video_frame_plane))
    );
    assert_eq!(
        ::std::mem::align_of::<video_frame_plane>(),
        4usize,
        concat!("Alignment of ", stringify!(video_frame_plane))
    );
    assert_eq!(
        unsafe { &(*(::std::ptr::null::<video_frame_plane>())).offset as *const _ as usize },
        0usize,
        concat!(
            "Offset of field: ",
            stringify!(video_frame_plane),
            "::",
            stringify!(offset)
        )
    );
    assert_eq!(
        unsafe { &(*(::std::ptr::null::<video_frame_plane>())).stride as *const _ as usize },
        4usize,
        concat!(
            "Offset of field: ",
            stringify!(video_frame_plane),
            "::",
            stringify!(stride)
        )
    );
}
pub type video_frame_plane_t = video_frame_plane;
pub const video_codec_profile_VIDEO_CODEC_PROFILE_UNKNOWN: video_codec_profile = -1;
pub const video_codec_profile_VIDEO_CODEC_PROFILE_MIN: video_codec_profile = -1;
pub const video_codec_profile_H264PROFILE_MIN: video_codec_profile = 0;
pub const video_codec_profile_H264PROFILE_BASELINE: video_codec_profile = 0;
pub const video_codec_profile_H264PROFILE_MAIN: video_codec_profile = 1;
pub const video_codec_profile_H264PROFILE_EXTENDED: video_codec_profile = 2;
pub const video_codec_profile_H264PROFILE_HIGH: video_codec_profile = 3;
pub const video_codec_profile_H264PROFILE_HIGH10PROFILE: video_codec_profile = 4;
pub const video_codec_profile_H264PROFILE_HIGH422PROFILE: video_codec_profile = 5;
pub const video_codec_profile_H264PROFILE_HIGH444PREDICTIVEPROFILE: video_codec_profile = 6;
pub const video_codec_profile_H264PROFILE_SCALABLEBASELINE: video_codec_profile = 7;
pub const video_codec_profile_H264PROFILE_SCALABLEHIGH: video_codec_profile = 8;
pub const video_codec_profile_H264PROFILE_STEREOHIGH: video_codec_profile = 9;
pub const video_codec_profile_H264PROFILE_MULTIVIEWHIGH: video_codec_profile = 10;
pub const video_codec_profile_H264PROFILE_MAX: video_codec_profile = 10;
pub const video_codec_profile_VP8PROFILE_MIN: video_codec_profile = 11;
pub const video_codec_profile_VP8PROFILE_ANY: video_codec_profile = 11;
pub const video_codec_profile_VP8PROFILE_MAX: video_codec_profile = 11;
pub const video_codec_profile_VP9PROFILE_MIN: video_codec_profile = 12;
pub const video_codec_profile_VP9PROFILE_PROFILE0: video_codec_profile = 12;
pub const video_codec_profile_VP9PROFILE_PROFILE1: video_codec_profile = 13;
pub const video_codec_profile_VP9PROFILE_PROFILE2: video_codec_profile = 14;
pub const video_codec_profile_VP9PROFILE_PROFILE3: video_codec_profile = 15;
pub const video_codec_profile_VP9PROFILE_MAX: video_codec_profile = 15;
pub const video_codec_profile_HEVCPROFILE_MIN: video_codec_profile = 16;
pub const video_codec_profile_HEVCPROFILE_MAIN: video_codec_profile = 16;
pub const video_codec_profile_HEVCPROFILE_MAIN10: video_codec_profile = 17;
pub const video_codec_profile_HEVCPROFILE_MAIN_STILL_PICTURE: video_codec_profile = 18;
pub const video_codec_profile_HEVCPROFILE_MAX: video_codec_profile = 18;
pub const video_codec_profile_DOLBYVISION_MIN: video_codec_profile = 19;
pub const video_codec_profile_DOLBYVISION_PROFILE0: video_codec_profile = 19;
pub const video_codec_profile_DOLBYVISION_PROFILE4: video_codec_profile = 20;
pub const video_codec_profile_DOLBYVISION_PROFILE5: video_codec_profile = 21;
pub const video_codec_profile_DOLBYVISION_PROFILE7: video_codec_profile = 22;
pub const video_codec_profile_DOLBYVISION_MAX: video_codec_profile = 22;
pub const video_codec_profile_THEORAPROFILE_MIN: video_codec_profile = 23;
pub const video_codec_profile_THEORAPROFILE_ANY: video_codec_profile = 23;
pub const video_codec_profile_THEORAPROFILE_MAX: video_codec_profile = 23;
pub const video_codec_profile_AV1PROFILE_MIN: video_codec_profile = 24;
pub const video_codec_profile_AV1PROFILE_PROFILE_MAIN: video_codec_profile = 24;
pub const video_codec_profile_AV1PROFILE_PROFILE_HIGH: video_codec_profile = 25;
pub const video_codec_profile_AV1PROFILE_PROFILE_PRO: video_codec_profile = 26;
pub const video_codec_profile_AV1PROFILE_MAX: video_codec_profile = 26;
pub const video_codec_profile_VIDEO_CODEC_PROFILE_MAX: video_codec_profile = 26;
pub type video_codec_profile = i32;
pub use self::video_codec_profile as video_codec_profile_t;
pub const video_pixel_format_YV12: video_pixel_format = 0;
pub const video_pixel_format_NV12: video_pixel_format = 1;
pub const video_pixel_format_PIXEL_FORMAT_MAX: video_pixel_format = 1;
pub type video_pixel_format = u32;
pub use self::video_pixel_format as video_pixel_format_t;
pub const vea_impl_type_VEA_FAKE: vea_impl_type = 0;
pub const vea_impl_type_GAVEA: vea_impl_type = 1;
pub type vea_impl_type = u32;
pub use self::vea_impl_type as vea_impl_type_t;
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct vea_profile {
    pub profile: video_codec_profile_t,
    pub max_width: u32,
    pub max_height: u32,
    pub max_framerate_numerator: u32,
    pub max_framerate_denominator: u32,
}
#[test]
fn bindgen_test_layout_vea_profile() {
    assert_eq!(
        ::std::mem::size_of::<vea_profile>(),
        20usize,
        concat!("Size of: ", stringify!(vea_profile))
    );
    assert_eq!(
        ::std::mem::align_of::<vea_profile>(),
        4usize,
        concat!("Alignment of ", stringify!(vea_profile))
    );
    assert_eq!(
        unsafe { &(*(::std::ptr::null::<vea_profile>())).profile as *const _ as usize },
        0usize,
        concat!(
            "Offset of field: ",
            stringify!(vea_profile),
            "::",
            stringify!(profile)
        )
    );
    assert_eq!(
        unsafe { &(*(::std::ptr::null::<vea_profile>())).max_width as *const _ as usize },
        4usize,
        concat!(
            "Offset of field: ",
            stringify!(vea_profile),
            "::",
            stringify!(max_width)
        )
    );
    assert_eq!(
        unsafe { &(*(::std::ptr::null::<vea_profile>())).max_height as *const _ as usize },
        8usize,
        concat!(
            "Offset of field: ",
            stringify!(vea_profile),
            "::",
            stringify!(max_height)
        )
    );
    assert_eq!(
        unsafe {
            &(*(::std::ptr::null::<vea_profile>())).max_framerate_numerator as *const _ as usize
        },
        12usize,
        concat!(
            "Offset of field: ",
            stringify!(vea_profile),
            "::",
            stringify!(max_framerate_numerator)
        )
    );
    assert_eq!(
        unsafe {
            &(*(::std::ptr::null::<vea_profile>())).max_framerate_denominator as *const _ as usize
        },
        16usize,
        concat!(
            "Offset of field: ",
            stringify!(vea_profile),
            "::",
            stringify!(max_framerate_denominator)
        )
    );
}
pub type vea_profile_t = vea_profile;
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct vea_capabilities {
    pub num_input_formats: usize,
    pub input_formats: *const video_pixel_format_t,
    pub num_output_formats: usize,
    pub output_formats: *const vea_profile_t,
}
#[test]
fn bindgen_test_layout_vea_capabilities() {
    assert_eq!(
        ::std::mem::size_of::<vea_capabilities>(),
        32usize,
        concat!("Size of: ", stringify!(vea_capabilities))
    );
    assert_eq!(
        ::std::mem::align_of::<vea_capabilities>(),
        8usize,
        concat!("Alignment of ", stringify!(vea_capabilities))
    );
    assert_eq!(
        unsafe {
            &(*(::std::ptr::null::<vea_capabilities>())).num_input_formats as *const _ as usize
        },
        0usize,
        concat!(
            "Offset of field: ",
            stringify!(vea_capabilities),
            "::",
            stringify!(num_input_formats)
        )
    );
    assert_eq!(
        unsafe { &(*(::std::ptr::null::<vea_capabilities>())).input_formats as *const _ as usize },
        8usize,
        concat!(
            "Offset of field: ",
            stringify!(vea_capabilities),
            "::",
            stringify!(input_formats)
        )
    );
    assert_eq!(
        unsafe {
            &(*(::std::ptr::null::<vea_capabilities>())).num_output_formats as *const _ as usize
        },
        16usize,
        concat!(
            "Offset of field: ",
            stringify!(vea_capabilities),
            "::",
            stringify!(num_output_formats)
        )
    );
    assert_eq!(
        unsafe { &(*(::std::ptr::null::<vea_capabilities>())).output_formats as *const _ as usize },
        24usize,
        concat!(
            "Offset of field: ",
            stringify!(vea_capabilities),
            "::",
            stringify!(output_formats)
        )
    );
}
pub type vea_capabilities_t = vea_capabilities;
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct vea_config {
    pub input_format: video_pixel_format_t,
    pub input_visible_width: u32,
    pub input_visible_height: u32,
    pub output_profile: video_codec_profile_t,
    pub initial_bitrate: u32,
    pub initial_framerate: u32,
    pub has_initial_framerate: u8,
    pub h264_output_level: u8,
    pub has_h264_output_level: u8,
}
#[test]
fn bindgen_test_layout_vea_config() {
    assert_eq!(
        ::std::mem::size_of::<vea_config>(),
        28usize,
        concat!("Size of: ", stringify!(vea_config))
    );
    assert_eq!(
        ::std::mem::align_of::<vea_config>(),
        4usize,
        concat!("Alignment of ", stringify!(vea_config))
    );
    assert_eq!(
        unsafe { &(*(::std::ptr::null::<vea_config>())).input_format as *const _ as usize },
        0usize,
        concat!(
            "Offset of field: ",
            stringify!(vea_config),
            "::",
            stringify!(input_format)
        )
    );
    assert_eq!(
        unsafe { &(*(::std::ptr::null::<vea_config>())).input_visible_width as *const _ as usize },
        4usize,
        concat!(
            "Offset of field: ",
            stringify!(vea_config),
            "::",
            stringify!(input_visible_width)
        )
    );
    assert_eq!(
        unsafe { &(*(::std::ptr::null::<vea_config>())).input_visible_height as *const _ as usize },
        8usize,
        concat!(
            "Offset of field: ",
            stringify!(vea_config),
            "::",
            stringify!(input_visible_height)
        )
    );
    assert_eq!(
        unsafe { &(*(::std::ptr::null::<vea_config>())).output_profile as *const _ as usize },
        12usize,
        concat!(
            "Offset of field: ",
            stringify!(vea_config),
            "::",
            stringify!(output_profile)
        )
    );
    assert_eq!(
        unsafe { &(*(::std::ptr::null::<vea_config>())).initial_bitrate as *const _ as usize },
        16usize,
        concat!(
            "Offset of field: ",
            stringify!(vea_config),
            "::",
            stringify!(initial_bitrate)
        )
    );
    assert_eq!(
        unsafe { &(*(::std::ptr::null::<vea_config>())).initial_framerate as *const _ as usize },
        20usize,
        concat!(
            "Offset of field: ",
            stringify!(vea_config),
            "::",
            stringify!(initial_framerate)
        )
    );
    assert_eq!(
        unsafe {
            &(*(::std::ptr::null::<vea_config>())).has_initial_framerate as *const _ as usize
        },
        24usize,
        concat!(
            "Offset of field: ",
            stringify!(vea_config),
            "::",
            stringify!(has_initial_framerate)
        )
    );
    assert_eq!(
        unsafe { &(*(::std::ptr::null::<vea_config>())).h264_output_level as *const _ as usize },
        25usize,
        concat!(
            "Offset of field: ",
            stringify!(vea_config),
            "::",
            stringify!(h264_output_level)
        )
    );
    assert_eq!(
        unsafe {
            &(*(::std::ptr::null::<vea_config>())).has_h264_output_level as *const _ as usize
        },
        26usize,
        concat!(
            "Offset of field: ",
            stringify!(vea_config),
            "::",
            stringify!(has_h264_output_level)
        )
    );
}
pub type vea_config_t = vea_config;
pub const vea_error_ILLEGAL_STATE_ERROR: vea_error = 0;
pub const vea_error_INVALID_ARGUMENT_ERROR: vea_error = 1;
pub const vea_error_PLATFORM_FAILURE_ERROR: vea_error = 2;
pub type vea_error = u32;
pub use self::vea_error as vea_error_t;
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct vea_session_info {
    pub ctx: *mut ::std::os::raw::c_void,
    pub event_pipe_fd: ::std::os::raw::c_int,
}
#[test]
fn bindgen_test_layout_vea_session_info() {
    assert_eq!(
        ::std::mem::size_of::<vea_session_info>(),
        16usize,
        concat!("Size of: ", stringify!(vea_session_info))
    );
    assert_eq!(
        ::std::mem::align_of::<vea_session_info>(),
        8usize,
        concat!("Alignment of ", stringify!(vea_session_info))
    );
    assert_eq!(
        unsafe { &(*(::std::ptr::null::<vea_session_info>())).ctx as *const _ as usize },
        0usize,
        concat!(
            "Offset of field: ",
            stringify!(vea_session_info),
            "::",
            stringify!(ctx)
        )
    );
    assert_eq!(
        unsafe { &(*(::std::ptr::null::<vea_session_info>())).event_pipe_fd as *const _ as usize },
        8usize,
        concat!(
            "Offset of field: ",
            stringify!(vea_session_info),
            "::",
            stringify!(event_pipe_fd)
        )
    );
}
pub type vea_session_info_t = vea_session_info;
pub type vea_input_buffer_id_t = i32;
pub type vea_output_buffer_id_t = i32;
pub const vea_event_type_REQUIRE_INPUT_BUFFERS: vea_event_type = 0;
pub const vea_event_type_PROCESSED_INPUT_BUFFER: vea_event_type = 1;
pub const vea_event_type_PROCESSED_OUTPUT_BUFFER: vea_event_type = 2;
pub const vea_event_type_VEA_FLUSH_RESPONSE: vea_event_type = 3;
pub const vea_event_type_VEA_NOTIFY_ERROR: vea_event_type = 4;
pub type vea_event_type = u32;
pub use self::vea_event_type as vea_event_type_t;
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct vea_require_input_buffers_event_data {
    pub input_count: u32,
    pub input_frame_width: u32,
    pub input_frame_height: u32,
    pub output_buffer_size: u32,
}
#[test]
fn bindgen_test_layout_vea_require_input_buffers_event_data() {
    assert_eq!(
        ::std::mem::size_of::<vea_require_input_buffers_event_data>(),
        16usize,
        concat!(
            "Size of: ",
            stringify!(vea_require_input_buffers_event_data)
        )
    );
    assert_eq!(
        ::std::mem::align_of::<vea_require_input_buffers_event_data>(),
        4usize,
        concat!(
            "Alignment of ",
            stringify!(vea_require_input_buffers_event_data)
        )
    );
    assert_eq!(
        unsafe {
            &(*(::std::ptr::null::<vea_require_input_buffers_event_data>())).input_count as *const _
                as usize
        },
        0usize,
        concat!(
            "Offset of field: ",
            stringify!(vea_require_input_buffers_event_data),
            "::",
            stringify!(input_count)
        )
    );
    assert_eq!(
        unsafe {
            &(*(::std::ptr::null::<vea_require_input_buffers_event_data>())).input_frame_width
                as *const _ as usize
        },
        4usize,
        concat!(
            "Offset of field: ",
            stringify!(vea_require_input_buffers_event_data),
            "::",
            stringify!(input_frame_width)
        )
    );
    assert_eq!(
        unsafe {
            &(*(::std::ptr::null::<vea_require_input_buffers_event_data>())).input_frame_height
                as *const _ as usize
        },
        8usize,
        concat!(
            "Offset of field: ",
            stringify!(vea_require_input_buffers_event_data),
            "::",
            stringify!(input_frame_height)
        )
    );
    assert_eq!(
        unsafe {
            &(*(::std::ptr::null::<vea_require_input_buffers_event_data>())).output_buffer_size
                as *const _ as usize
        },
        12usize,
        concat!(
            "Offset of field: ",
            stringify!(vea_require_input_buffers_event_data),
            "::",
            stringify!(output_buffer_size)
        )
    );
}
pub type vea_require_input_buffers_event_data_t = vea_require_input_buffers_event_data;
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct vea_processed_output_buffer_event_data {
    pub output_buffer_id: vea_output_buffer_id_t,
    pub payload_size: u32,
    pub key_frame: u8,
    pub timestamp: i64,
}
#[test]
fn bindgen_test_layout_vea_processed_output_buffer_event_data() {
    assert_eq!(
        ::std::mem::size_of::<vea_processed_output_buffer_event_data>(),
        24usize,
        concat!(
            "Size of: ",
            stringify!(vea_processed_output_buffer_event_data)
        )
    );
    assert_eq!(
        ::std::mem::align_of::<vea_processed_output_buffer_event_data>(),
        8usize,
        concat!(
            "Alignment of ",
            stringify!(vea_processed_output_buffer_event_data)
        )
    );
    assert_eq!(
        unsafe {
            &(*(::std::ptr::null::<vea_processed_output_buffer_event_data>())).output_buffer_id
                as *const _ as usize
        },
        0usize,
        concat!(
            "Offset of field: ",
            stringify!(vea_processed_output_buffer_event_data),
            "::",
            stringify!(output_buffer_id)
        )
    );
    assert_eq!(
        unsafe {
            &(*(::std::ptr::null::<vea_processed_output_buffer_event_data>())).payload_size
                as *const _ as usize
        },
        4usize,
        concat!(
            "Offset of field: ",
            stringify!(vea_processed_output_buffer_event_data),
            "::",
            stringify!(payload_size)
        )
    );
    assert_eq!(
        unsafe {
            &(*(::std::ptr::null::<vea_processed_output_buffer_event_data>())).key_frame as *const _
                as usize
        },
        8usize,
        concat!(
            "Offset of field: ",
            stringify!(vea_processed_output_buffer_event_data),
            "::",
            stringify!(key_frame)
        )
    );
    assert_eq!(
        unsafe {
            &(*(::std::ptr::null::<vea_processed_output_buffer_event_data>())).timestamp as *const _
                as usize
        },
        16usize,
        concat!(
            "Offset of field: ",
            stringify!(vea_processed_output_buffer_event_data),
            "::",
            stringify!(timestamp)
        )
    );
}
pub type vea_processed_output_buffer_event_data_t = vea_processed_output_buffer_event_data;
#[repr(C)]
#[derive(Copy, Clone)]
pub union vea_event_data {
    pub require_input_buffers: vea_require_input_buffers_event_data_t,
    pub processed_input_buffer_id: vea_input_buffer_id_t,
    pub processed_output_buffer: vea_processed_output_buffer_event_data_t,
    pub flush_done: u8,
    pub error: vea_error_t,
    _bindgen_union_align: [u64; 3usize],
}
#[test]
fn bindgen_test_layout_vea_event_data() {
    assert_eq!(
        ::std::mem::size_of::<vea_event_data>(),
        24usize,
        concat!("Size of: ", stringify!(vea_event_data))
    );
    assert_eq!(
        ::std::mem::align_of::<vea_event_data>(),
        8usize,
        concat!("Alignment of ", stringify!(vea_event_data))
    );
    assert_eq!(
        unsafe {
            &(*(::std::ptr::null::<vea_event_data>())).require_input_buffers as *const _ as usize
        },
        0usize,
        concat!(
            "Offset of field: ",
            stringify!(vea_event_data),
            "::",
            stringify!(require_input_buffers)
        )
    );
    assert_eq!(
        unsafe {
            &(*(::std::ptr::null::<vea_event_data>())).processed_input_buffer_id as *const _
                as usize
        },
        0usize,
        concat!(
            "Offset of field: ",
            stringify!(vea_event_data),
            "::",
            stringify!(processed_input_buffer_id)
        )
    );
    assert_eq!(
        unsafe {
            &(*(::std::ptr::null::<vea_event_data>())).processed_output_buffer as *const _ as usize
        },
        0usize,
        concat!(
            "Offset of field: ",
            stringify!(vea_event_data),
            "::",
            stringify!(processed_output_buffer)
        )
    );
    assert_eq!(
        unsafe { &(*(::std::ptr::null::<vea_event_data>())).flush_done as *const _ as usize },
        0usize,
        concat!(
            "Offset of field: ",
            stringify!(vea_event_data),
            "::",
            stringify!(flush_done)
        )
    );
    assert_eq!(
        unsafe { &(*(::std::ptr::null::<vea_event_data>())).error as *const _ as usize },
        0usize,
        concat!(
            "Offset of field: ",
            stringify!(vea_event_data),
            "::",
            stringify!(error)
        )
    );
}
pub type vea_event_data_t = vea_event_data;
#[repr(C)]
#[derive(Copy, Clone)]
pub struct vea_event {
    pub event_type: vea_event_type_t,
    pub event_data: vea_event_data_t,
}
#[test]
fn bindgen_test_layout_vea_event() {
    assert_eq!(
        ::std::mem::size_of::<vea_event>(),
        32usize,
        concat!("Size of: ", stringify!(vea_event))
    );
    assert_eq!(
        ::std::mem::align_of::<vea_event>(),
        8usize,
        concat!("Alignment of ", stringify!(vea_event))
    );
    assert_eq!(
        unsafe { &(*(::std::ptr::null::<vea_event>())).event_type as *const _ as usize },
        0usize,
        concat!(
            "Offset of field: ",
            stringify!(vea_event),
            "::",
            stringify!(event_type)
        )
    );
    assert_eq!(
        unsafe { &(*(::std::ptr::null::<vea_event>())).event_data as *const _ as usize },
        8usize,
        concat!(
            "Offset of field: ",
            stringify!(vea_event),
            "::",
            stringify!(event_data)
        )
    );
}
pub type vea_event_t = vea_event;
extern "C" {
    pub fn initialize_encode(type_: vea_impl_type_t) -> *mut ::std::os::raw::c_void;
}
extern "C" {
    pub fn deinitialize_encode(impl_: *mut ::std::os::raw::c_void);
}
extern "C" {
    pub fn get_vea_capabilities(impl_: *mut ::std::os::raw::c_void) -> *const vea_capabilities_t;
}
extern "C" {
    pub fn init_encode_session(
        impl_: *mut ::std::os::raw::c_void,
        config: *mut vea_config_t,
    ) -> *mut vea_session_info_t;
}
extern "C" {
    pub fn close_encode_session(
        impl_: *mut ::std::os::raw::c_void,
        session_info: *mut vea_session_info_t,
    );
}
extern "C" {
    pub fn vea_encode(
        ctx: *mut ::std::os::raw::c_void,
        input_buffer_id: vea_input_buffer_id_t,
        fd: ::std::os::raw::c_int,
        num_planes: usize,
        planes: *mut video_frame_plane_t,
        timestamp: i64,
        force_keyframe: u8,
    ) -> ::std::os::raw::c_int;
}
extern "C" {
    pub fn vea_use_output_buffer(
        ctx: *mut ::std::os::raw::c_void,
        output_buffer_id: vea_output_buffer_id_t,
        fd: ::std::os::raw::c_int,
        offset: u32,
        size: u32,
    ) -> ::std::os::raw::c_int;
}
extern "C" {
    pub fn vea_request_encoding_params_change(
        ctx: *mut ::std::os::raw::c_void,
        bitrate: u32,
        framerate: u32,
    ) -> ::std::os::raw::c_int;
}
extern "C" {
    pub fn vea_flush(ctx: *mut ::std::os::raw::c_void) -> ::std::os::raw::c_int;
}
