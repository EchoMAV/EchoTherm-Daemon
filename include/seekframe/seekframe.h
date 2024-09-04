//
// SEEK THERMAL CONFIDENTIAL
// _________________________
//
// Copyright (c) 2021 Seek Thermal Incorporated
// All Rights Reserved.
//
// NOTICE:  All information contained herein is, and remains
// the property of Seek Thermal Incorporated and its suppliers,
// if any.  The size_tellectual and technical concepts contained
// herein are proprietary to Seek Thermal Incorporated
// and its suppliers and may be covered by U.S. and Foreign Patents,
// patents in process, and are protected by trade secret or copyright law.
// Dissemination of this information or reproduction of this material
// is strictly forbidden unless prior written permission is obtained
// from Seek Thermal Incorporated.
//

#ifndef __SEEKFRAME_H__
#define __SEEKFRAME_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>

//------------------------------------------------------------------------------
// Export
//------------------------------------------------------------------------------
#ifndef SEEKFRAME_API
#	ifdef __cplusplus
#		if defined(WIN32)
#			define SEEKFRAME_API extern "C" __declspec(dllexport)
#		else
#			define SEEKFRAME_API extern "C" __attribute__((__visibility__("default")))
#		endif
#	else
#		if defined(WIN32)
#			define SEEKFRAME_API extern __declspec(dllexport)
#		else
#			define SEEKFRAME_API extern __attribute__((__visibility__("default")))
#		endif
#	endif
#endif

//------------------------------------------------------------------------------
// Types
//------------------------------------------------------------------------------
// Structure that represents an arbitrary frame.
typedef struct seekframe_t seekframe_t;

//------------------------------------------------------------------------------
// Functions
//------------------------------------------------------------------------------
// Gets the width of the frame in image coordinates.
SEEKFRAME_API size_t seekframe_get_width(
	const seekframe_t* frame);

// Gets the height of the frame in image coordinates.
SEEKFRAME_API size_t seekframe_get_height(
	const seekframe_t* frame);

// Gets the number of image channels of the frame.
SEEKFRAME_API size_t seekframe_get_channels(
	const seekframe_t* frame);

// Gets the pixel depth of the frame in pixels.
SEEKFRAME_API size_t seekframe_get_pixel_depth(
	const seekframe_t* frame);

// Gets the pixel padding of the frame in bits.
SEEKFRAME_API size_t seekframe_get_pixel_padding(
	const seekframe_t* frame);

// Gets the line stride of the frame in bytes.
SEEKFRAME_API size_t seekframe_get_line_stride(
	const seekframe_t* frame);

// Gets the line padding of the frame in bytes.
SEEKFRAME_API size_t seekframe_get_line_padding(
	const seekframe_t* frame);

// Gets the total size of the frame pixel data in bytes.
SEEKFRAME_API size_t seekframe_get_data_size(
	const seekframe_t* frame);

// Gets the pointer to the pixel data of the frame.
SEEKFRAME_API void* seekframe_get_data(
	const seekframe_t* frame);

// Gets the pointer to a row of pixel data of the frame.
SEEKFRAME_API void* seekframe_get_row(
	const seekframe_t* frame,
	size_t y);

// Gets the pointer to an individual pixel of the frame.
SEEKFRAME_API void* seekframe_get_pixel(
	const seekframe_t* frame,
	size_t x,
	size_t y);

// Checks if the frame does not contain any data.
SEEKFRAME_API bool seekframe_is_empty(
	const seekframe_t* frame);

// Gets the total size of the frame header in bytes.
SEEKFRAME_API size_t seekframe_get_header_size(
	const seekframe_t* frame);

// Gets the pointer to the frame header.
SEEKFRAME_API void* seekframe_get_header(
	const seekframe_t* frame);

#ifdef __cplusplus
}
#endif
#endif /* __SEEKFRAME_H__ */
