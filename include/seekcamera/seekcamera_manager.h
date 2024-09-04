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
#ifndef __SEEKCAMERA_MANAGER_H__
#define __SEEKCAMERA_MANAGER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "seekcamera/seekcamera.h"
#include "seekcamera/seekcamera_error.h"

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
// Structure that manages Seek cameras.
typedef struct seekcamera_manager_t seekcamera_manager_t;

// Enumerated type representing types of events used by the camera manager.
typedef enum seekcamera_manager_event_t
{
	SEEKCAMERA_MANAGER_EVENT_CONNECT = 0,
	SEEKCAMERA_MANAGER_EVENT_DISCONNECT,
	SEEKCAMERA_MANAGER_EVENT_ERROR,
	SEEKCAMERA_MANAGER_EVENT_READY_TO_PAIR,
} seekcamera_manager_event_t;

// Callback function type that is fired every time the camera manager receives an event.
typedef void (*seekcamera_manager_event_callback_t)(
	seekcamera_t* camera,
	seekcamera_manager_event_t event,
	seekcamera_error_t event_status,
	void* user_data);

//-----------------------------------------------------------------------------
// Functions
//-----------------------------------------------------------------------------
// Creates the camera manager responsible for managing Seek cameras.
SEEKCAMERA_API seekcamera_error_t seekcamera_manager_create(
	seekcamera_manager_t** camera_manager,
	uint32_t discovery_mode);

// Destroys and invalidates an existing camera manager.
SEEKCAMERA_API seekcamera_error_t seekcamera_manager_destroy(
	seekcamera_manager_t** camera_manager);

// Registers a user-defined event callback function with the camera manager.
// The callback is fired every time the camera manager receives an event.
SEEKCAMERA_API seekcamera_error_t seekcamera_manager_register_event_callback(
	seekcamera_manager_t* camera_manager,
	seekcamera_manager_event_callback_t callback,
	void* user_data);

// Gets the description of a camera manager event type.
SEEKCAMERA_API const char* seekcamera_manager_get_event_str(
	seekcamera_manager_event_t event);

#ifdef __cplusplus
}
#endif
#endif /* __SEEKCAMERA_MANAGER_H__ */
