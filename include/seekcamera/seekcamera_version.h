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

#ifndef __SEEKCAMERA_VERSION_H__
#define __SEEKCAMERA_VERSION_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

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
// Functions
//-----------------------------------------------------------------------------
// Returns the major version number of the seekcamera API.
// The version number scheme is [MAJOR].[MINOR].[PATCH].[INTERNAL]_[QUALIFIER]
SEEKCAMERA_API uint32_t seekcamera_version_get_major();

// Returns the minor version number of the seekcamera API.
// The version number scheme is [MAJOR].[MINOR].[PATCH].[INTERNAL]_[QUALIFIER]
SEEKCAMERA_API uint32_t seekcamera_version_get_minor();

// Returns the patch version number of the seekcamera API.
// The version number scheme is [MAJOR].[MINOR].[PATCH].[INTERNAL]_[QUALIFIER]
SEEKCAMERA_API uint32_t seekcamera_version_get_patch();

// Returns the internal version number of the seekcamera API.
// The version number scheme is [MAJOR].[MINOR].[PATCH].[INTERNAL]_[QUALIFIER]
SEEKCAMERA_API uint32_t seekcamera_version_get_internal();

// Returns the qualifier version string of the seekcamera API.
// The version number scheme is [MAJOR].[MINOR].[PATCH].[INTERNAL]_[QUALIFIER]
SEEKCAMERA_API const char* seekcamera_version_get_qualifier();

#ifdef __cplusplus
}
#endif
#endif /*__SEEKCAMERA_VERSION_H__*/
