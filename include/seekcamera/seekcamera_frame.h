//
// SEEK THERMAL CONFIDENTIAL
// _________________________
//
// Copyright (c) 2021 Seek Thermal Incorporated
// All Rights Reserved.
//
// NOTICE:  All information contained herein is, and remains
// the property of Seek Thermal Incorporated and its suppliers,
// if any.  The intellectual and technical concepts contained
// herein are proprietary to Seek Thermal Incorporated
// and its suppliers and may be covered by U.S. and Foreign Patents,
// patents in process, and are protected by trade secret or copyright law.
// Dissemination of this information or reproduction of this material
// is strictly forbidden unless prior written permission is obtained
// from Seek Thermal Incorporated.
//

#ifndef __SEEKCAMERA_FRAME_H__
#define __SEEKCAMERA_FRAME_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "seekcamera/seekcamera_error.h"
#include "seekframe/seekframe.h"

//-----------------------------------------------------------------------------
// Export
//-----------------------------------------------------------------------------
#ifndef SEEKCAMERA_API
#	ifdef __cplusplus
#		if defined(WIN32)
#			define SEEKCAMERA_API extern "C" __declspec(dllexport)
#		else
#			define SEEKCAMERA_API extern "C" __attribute__((__visibility__("default")))
#		endif
#	else
#		if defined(WIN32)
#			define SEEKCAMERA_API extern __declspec(dllexport)
#		else
#			define SEEKCAMERA_API extern __attribute__((__visibility__("default")))
#		endif
#	endif
#endif

//-----------------------------------------------------------------------------
// Types
//-----------------------------------------------------------------------------
// Structure that encapsulates single or multiple generic seekframe_t structs.
typedef struct seekcamera_frame_t seekcamera_frame_t;

// Types of output frame formats.
typedef enum seekcamera_frame_format_t
{
	SEEKCAMERA_FRAME_FORMAT_CORRECTED = 0x04,
	SEEKCAMERA_FRAME_FORMAT_PRE_AGC = 0x08,
	SEEKCAMERA_FRAME_FORMAT_THERMOGRAPHY_FLOAT = 0x10,
	SEEKCAMERA_FRAME_FORMAT_THERMOGRAPHY_FIXED_10_6 = 0x20,
	SEEKCAMERA_FRAME_FORMAT_GRAYSCALE = 0x40,
	SEEKCAMERA_FRAME_FORMAT_COLOR_ARGB8888 = 0x80,
	SEEKCAMERA_FRAME_FORMAT_COLOR_RGB565 = 0x100,
	SEEKCAMERA_FRAME_FORMAT_COLOR_AYUV = 0x200,
	SEEKCAMERA_FRAME_FORMAT_COLOR_YUY2 = 0x400,
} seekcamera_frame_format_t;

#pragma pack(push, 1)

// Common header for the camera frame.
// It is a fixed size (2048 bytes) and byte aligned.
typedef struct seekcamera_frame_header_t
{
	uint32_t sentinel;                          // Header sentinel
	uint8_t version;                            // Version of the frame header
	uint32_t type;                              // Enumerated frame type (seekcamera_frame_format_t)
	uint16_t width;                             // Number of pixels in horizontal dimension
	uint16_t height;                            // Number of pixels in vertical dimension
	uint8_t channels;                           // Number of image channels (e.g. 3 for RGB)
	uint8_t pixel_depth;                        // Number of bits per pixel
	uint8_t pixel_padding;                      // Number of padding bits per pixel
	uint16_t line_stride;                       // Number of bytes per line (i.e. row)
	uint16_t line_padding;                      // Number of padding bytes per line (i.e. row)
	uint16_t header_size;                       // Number of bytes in the header including line padding
	uint64_t timestamp_utc_ns;                  // UTC timestamp in nanosecond resolution
	char chipid[16];                            // CID of the camera (seekcamera_chipid_t)
	char serial_number[16];                     // SN of the camera (seekcamera_serial_number_t)
	char core_part_number[32];                  // CPN of the camera (seekcamera_core_part_number_t)
	uint8_t firmware_version[4];                // Firmware version (seekcamera_firmware_version_t)
	uint8_t io_type;                            // IO type of the camera (seekcamera_io_type_t)
	uint32_t fpa_frame_count;                   // Index of the frame as seen by the FPA
	uint32_t fpa_diode_count;                   // Uncalibrated sampling of the FPA temperature diode voltage
	float environment_temperature;              // Estimated temperature based on the FPA in degrees Celsius
	uint16_t thermography_min_x;                // Image coordinate (x-dimension) of the min thermography pixel
	uint16_t thermography_min_y;                // Image coordinate (y-dimension) of the min thermography pixel
	float thermography_min_value;               // Value of the min thermography pixel
	uint16_t thermography_max_x;                // Image coordinate (x-dimension) of the max thermography pixel
	uint16_t thermography_max_y;                // Image coordinate (y-dimension) of the max thermography pixel
	float thermography_max_value;               // Value of the max thermography pixel
	uint16_t thermography_spot_x;               // Image coordinate (x-dimension) of the 'spot' thermography pixel
	uint16_t thermography_spot_y;               // Image coordinate (y-dimension) of the 'spot' thermography pixel
	float thermography_spot_value;              // Value of the max thermography pixel
	uint8_t agc_mode;                           // AGC mode used to process the image (seekcamera_agc_mode_t)
	uint16_t histeq_agc_num_bins;               // Number of bins in the HistEQ AGC histogram.
	uint16_t histeq_agc_bin_width;              // Number of counts per bin in the HistEQ AGC histogram.
	float histeq_agc_gain_limit_factor;         // Multiplier of the HistEQ AGC gain limit.
	uint8_t histeq_agc_reserved[64];            // Reserved bytes for future HistEQ AGC metrics.
	float linear_agc_min;						// Minimum count value in the frame when using Linear AGC.
	float linear_agc_max;						// Maximum count value in the frame when using Linear AGC.
	uint8_t linear_agc_reserved[32];            // Reserved bytes for future Linear AGC metrics.
	uint8_t gradient_correction_filter_state;   // State of the gradient correction filter (seekcamera_filter_state)
	uint8_t flat_scene_correction_filter_state; // State of the flat scene correction filter (seekcamera_filter_state)
	uint8_t reserved[1798];                     // Reserved bytes to guarantee this struct is 2048 bytes
} seekcamera_frame_header_t;

#pragma pack(pop)

//-----------------------------------------------------------------------------
// Functions
//-----------------------------------------------------------------------------
// Gets an individual frame from the camera frame structure according to format.
SEEKCAMERA_API seekcamera_error_t seekcamera_frame_get_frame_by_format(
	const seekcamera_frame_t* camera_frame,
	seekcamera_frame_format_t format,
	seekframe_t** frame);

// Locks a camera frame for exclusive use by the calling thread.
SEEKCAMERA_API seekcamera_error_t seekcamera_frame_lock(
	seekcamera_frame_t* camera_frame);

// Unlocks a camera frame to allow access from multiple threads.
SEEKCAMERA_API seekcamera_error_t seekcamera_frame_unlock(
	seekcamera_frame_t* camera_frame);

#ifdef __cplusplus
}
#endif
#endif /* __SEEKCAMERA_FRAME_H__ */
