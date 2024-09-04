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

#ifndef __SEEKCAMERA_ERROR_H__
#define __SEEKCAMERA_ERROR_H__

#ifdef __cplusplus
extern "C" {
#endif

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
// Types of camera errors.
// This type is returned by most of the seekcamera API calls.
typedef enum seekcamera_error_t
{
	SEEKCAMERA_SUCCESS = 0,
	SEEKCAMERA_ERROR_DEVICE_COMMUNICATION = -1,
	SEEKCAMERA_ERROR_INVALID_PARAMETER = -2,
	SEEKCAMERA_ERROR_PERMISSIONS = -3,
	SEEKCAMERA_ERROR_NO_DEVICE = -4,
	SEEKCAMERA_ERROR_DEVICE_NOT_FOUND = -5,
	SEEKCAMERA_ERROR_DEVICE_BUSY = -6,
	SEEKCAMERA_ERROR_TIMEOUT = -7,
	SEEKCAMERA_ERROR_OVERFLOW = -8,
	SEEKCAMERA_ERROR_UNKNOWN_REQUEST = -9,
	SEEKCAMERA_ERROR_INTERRUPTED = -10,
	SEEKCAMERA_ERROR_OUT_OF_MEMORY = -11,
	SEEKCAMERA_ERROR_NOT_SUPPORTED = -12,
	SEEKCAMERA_ERROR_OTHER = -99,
	SEEKCAMERA_ERROR_CANNOT_PERFORM_REQUEST = -103,
	SEEKCAMERA_ERROR_FLASH_ACCESS_FAILURE = -104,
	SEEKCAMERA_ERROR_IMPLEMENTATION_ERROR = -105,
	SEEKCAMERA_ERROR_REQUEST_PENDING = -106,
	SEEKCAMERA_ERROR_INVALID_FIRMWARE_IMAGE = -107,
	SEEKCAMERA_ERROR_INVALID_KEY = -108,
	SEEKCAMERA_ERROR_SENSOR_COMMUNICATION = -109,
	SEEKCAMERA_ERROR_OUT_OF_RANGE = -301,
	SEEKCAMERA_ERROR_VERIFY_FAILED = -302,
	SEEKCAMERA_ERROR_SYSCALL_FAILED = -303,
	SEEKCAMERA_ERROR_FILE_DOES_NOT_EXIST = -400,
	SEEKCAMERA_ERROR_DIRECTORY_DOES_NOT_EXIST = -401,
	SEEKCAMERA_ERROR_FILE_READ_FAILED = -402,
	SEEKCAMERA_ERROR_FILE_WRITE_FAILED = -403,
	SEEKCAMERA_ERROR_NOT_IMPLEMENTED = -1000,
	SEEKCAMERA_ERROR_NOT_PAIRED = -1001,
} seekcamera_error_t;

//-----------------------------------------------------------------------------
// Functions
//-----------------------------------------------------------------------------
// Gets the description of a camera error type.
SEEKCAMERA_API const char* seekcamera_error_get_str(
	seekcamera_error_t status);

#ifdef __cplusplus
}
#endif
#endif /* __SEEKCAMERA_ERROR_H__ */
