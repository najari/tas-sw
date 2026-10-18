/*****************************************************************************
 *
 * Copyright (C) 2018-2025 Infineon Technologies AG. All rights reserved.
 *
 * Infineon Technologies AG (Infineon) is supplying this software for use with
 * Infineon's microcontrollers.  This file can be freely used for creating
 * development tools that are supporting such microcontrollers.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS".  NO WARRANTIES, WHETHER EXPRESS, IMPLIED
 * OR STATUTORY, INCLUDING, BUT NOT LIMITED TO, IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE APPLY TO THIS SOFTWARE.
 * INFINEON SHALL NOT, IN ANY CIRCUMSTANCES, BE LIABLE FOR DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES, FOR ANY REASON
 * WHATSOEVER.
 *
 ******************************************************************************
 * MODULE:  mcds_lib_recorder.h
 ******************************************************************************
 DESCRIPTION:
 MCDS recorder library
 Continuous trace is not supported for miniMCDS.
 ******************************************************************************/

#ifndef __mcds_lib_recorder_h_
#define __mcds_lib_recorder_h_

// This is a C interface, not a C++ interface.
#ifdef __cplusplus
extern "C" {
#endif

#include "mcd_types.h"
#include "mcd_api.h"  // For mcd_core_con_info_st

#include "mcds_lib.h"

//-------------------------------------------------------------------------------------------------
#define MCDSRL_COPYRIGHT_STRING "(c) Infineon Technologies AG 2025"
#define MCDSRL_NAME             "MCDS Recorder Library"

#define MCDSRL_VERSION_MAJOR 3  // 220217
#define MCDSRL_VERSION_MINOR 3  // 250403
#define MCDSRL_VERSION_PATCH 0  // 250403
// Version numbers will be read by mcds_recorder_das_dll.cmake


/*
	Changes V3.2 to V3.3 (250703)
	1. Added mcdsrl_read_timestamped() to improve MCDS frequency measurement
	2. mcdsrl_disconnect_device() can recover a lost device connection

	Changes V3.1 to V3.2 (240418)
	1. Uses TasClientTrc for continuous trace (requires TasServer V2.0 or higher)
	2. New TC4x devices supported

	Changes V3.0 to V3.1 (240418)
	1. Changed from DAS to TAS client server protocol (https://github.com/Infineon/tas_client_api)

	Changes V2.2 to V3.0 (220217)
	1. mcdsrl_mcds_frequency_measurement() removed. Can be implemented on top of recorder lib

	Changes V2.1 to V2.2
	1. Continuous trace: If overall less than one tile trace data is generated, this tile is now read.
	2. TC39x A-step not supported anymore
	3. Bug fixed in mcdsrl_mcds_frequency_measurement() (Release version did not work reliable)
	4. mcdsl_mcdsc_hdr_st changed (prepared for supporting different variants in future)

	Changes V1.0 to V2.1
	1. Moved reset control to mcdsl_mcdsc_st* mcdsc to simplify the recorder
	2. Removed reset and mode control parameters from mcdsrl_start_recording()
	3. Added the cleanup concept to mcdsrl_set_mcdsconfig() and mcdsrl_stop_recording()
	3. Removed mcdsrl_set_mcdsconfig_with_file(). Use configurator to read files
	4. Added mcdsrl_qry_devices()
	5. Added device/tool capability information mcdsrl_cplty_st to mcdsrl_device_connected()
	6. miniMCDS is supported but not for continuous trace modes
*/

//-------------------------------------------------------------------------------------------------
// Information about the recorded trace in CONTINUOUS trace modes.
// Can be also used as optional limits for stopping the trace recording.
// Whatever limit is reached first will end the trace recording.
// The trace recording can still be stopped at any time with mcdsrl_stop_recording().
// A value of 0 represents no limit
typedef struct {
  uint32_t trace_size_kb;       // Recorded trace [kByte]
  uint32_t recording_time_ms;   // Recording time [ms]
  uint32_t num_trace_gaps;      // Number of trace gaps in CONTINUOUS mode with MCTRL_RECBUF_FIFO_IFTG
} mcdsrl_trace_state_st;
// If num_trace_gaps is used as a limit, the value 1 means that the trace is stopped at the first overrun (gap).

//-------------------------------------------------------------------------------------------------
// Recording state
typedef enum _mcdsrl_recstate_et {
  MCDSRL_RECSTATE_IDLE       = 0,  // No trace recording
  MCDSRL_RECSTATE_PRE_TRIG   = 1,  // Trace is recorded (e.g. on-chip circular), trigger has not yet occurred.
  MCDSRL_RECSTATE_POST_TRIG  = 2,  // Trace is recorded after trigger occurred. Also if no trigger condition is defined.
  MCDSRL_RECSTATE_CONTINUOUS = 8,  // Continuous streaming trace mode
  MCDSRL_RECSTATE_UNKNOWN    = 0xFF,
} mcdsrl_recstate_et;

//-------------------------------------------------------------------------------------------------
// Device connection capabilities 
// This is a combination of device features, tool features and configuration (e.g. DAP frequency).
// A capability is a superset if it has a higher number 
typedef enum _mcdsrl_cplty_et {
	MCDSRL_CPLTY_UNKNOWN               = 0,  // E.g. no device connected
	MCDSRL_CPLTY_NO_TRACE              = 1,  // E.g. device without any trace support
	MCDSRL_CPLTY_ONCHIP_TRACE_ONLY     = 4,  // E.g. tool HW without continuous trace support
	MCDSRL_CPLTY_CONT_TRACE_UNRELIABLE = 8,  // E.g. miniWiggler with Windows PC
	MCDSRL_CPLTY_CONT_TRACE_RELIABLE   = 12, // E.g. tool HW for DAP or DXCPL
} mcdsrl_capability_et;

typedef struct {
	mcdsrl_capability_et  cplty;
	uint32_t         bandwidth_kb;  // Approximate bandwidth for continuous trace [kByte/s]
	uint32_t         trace_buf_kb;  // Guaranteed (TBUF, TRAM, XTM) on-chip trace buffer [kByte]
} mcdsrl_cplty_st;

//-------------------------------------------------------------------------------------------------
mcdsl_return_et mcdsrl_lib_init(uint8_t version_major);  
// version_major ensures compatibility

const mcdsl_version_st* mcdsrl_lib_version();

const mcd_char_t* mcdsrl_get_error();  // Returns the error string of the last error

mcdsl_return_et mcdsrl_qry_devices(const mcd_char_t* host, uint32_t max_num_devices, uint32_t* num_devices, mcd_core_con_info_st *con_device);
// If host is NULL a device connection for the local computer is assumed.
// max_num_devices is the size of con_device array, allocated by the caller.
// If max_num_devices is 0 *num_devices will return the number of devices found. 
// Otherwise the rule is *num_devices <= max_num_devices.
// Optional function, can return FUNCTION_NOT_SUPPORTED

mcdsl_return_et mcdsrl_connect_device(const mcd_core_con_info_st* con_requested, mcd_core_con_info_st* con_connected);
// If con_requested is NULL a device connection for the local computer is assumed
// con_connected will represent the established device connection
// Note: TC2xx and TC2xxED have the same JTAG ID (device_type) but can be distinguished by the name string (device)
// TC29x/TC29xED, TC27x/TC27xED, TC26x/TC26xED, TC23x/TC23xED
// If the DAP frequency is below 2MHz it will be increased automatically.
// Call mcdsrl_disconnect_device() before calling again to connect another device.
// Calling with con_requested value equal to the last con_connected will recover a lost connection.

const mcd_core_con_info_st* mcdsrl_device_connected(mcdsrl_cplty_st *cplty);
// Returns con_connected or NULL if no device is connected
// cplty can be NULL if not needed

mcdsl_return_et mcdsrl_disconnect_device();

mcdsl_return_et mcdsrl_reset_device(mcd_bool_t halt_after_reset);
// Resets the device with optional Halt After Reset (HAR)
// In case of HAR mcdsrl_start_recording() will start the device if mcdsc->crsv is MCDSL_CRSV_HAR

// The following read/write functions will return as error codes:
// MCDSL_RET_FUNCTION_USED_WRONGLY if no device was successfully connected before
// MCDSL_RET_TOOL_HW_CONNECTION, MCDSL_RET_DEVICE_CONNECTION
// MCDSL_RET_SUCCESS, MCDSL_RET_DEVICE_PORST, MCDSL_RET_DEVICE_DATA, MCDSL_RET_DEVICE_DATA_PORST

mcdsl_return_et mcdsrl_read( uint32_t addr, uint32_t* value);
mcdsl_return_et mcdsrl_write(uint32_t addr, uint32_t value);
// Reads/writes a single 32 bit word
// Device connection alive: MCDSL_RET_SUCCESS, MCDSL_RET_DEVICE_PORST
// Device connection alive but access failed: MCDSL_RET_DEVICE_DATA, MCDSL_RET_DEVICE_DATA_PORST

#define MCDSRL_MAX_LIST_SIZE 1024  // Maximum number of 32 bit reads/writes in one list
mcdsl_return_et mcdsrl_read_list( const uint32_t* addr, uint32_t* value, uint16_t num, uint16_t* num_ok);
mcdsl_return_et mcdsrl_write_list(const uint32_t* addr, const uint32_t* value, uint16_t num, uint16_t* num_ok);
// Reads/writes a list of 32 bit words
// Device connection alive: MCDSL_RET_SUCCESS, MCDSL_RET_DEVICE_PORST
// Device connection alive but an access failed: MCDSL_RET_DEVICE_DATA, MCDSL_RET_DEVICE_DATA_PORST
// *num_ok transactions can be used for all return values. 
// Example: *num_ok transactions will be 0 if the first access of the list fails inside of the device.

mcdsl_return_et mcdsrl_read_block( uint32_t addr, void* data, uint32_t num_bytes, uint32_t* num_bytes_ok);
mcdsl_return_et mcdsrl_write_block(uint32_t addr, const void* data, uint32_t num_bytes, uint32_t *num_bytes_ok);
// Reads/writes a 32 bit word aligned and sized block of data
// Device connection alive: MCDSL_RET_SUCCESS, MCDSL_RET_DEVICE_PORST
// Device connection alive but an access failed: MCDSL_RET_DEVICE_DATA, MCDSL_RET_DEVICE_DATA_PORST
// *num_bytes_ok can be used for all return values.
// Example: *num_bytes_ok will be 0 if the first access of the block fails inside of the device.

mcdsl_return_et mcdsrl_read_timestamped(uint32_t addr, uint32_t* value, uint64_t* ts_before, uint64_t* ts_after);
// Reads a single 32 bit word with TasServer timestamps before and after
// Device connection alive: MCDSL_RET_SUCCESS, MCDSL_RET_DEVICE_PORST
// Device connection alive but access failed: MCDSL_RET_DEVICE_DATA, MCDSL_RET_DEVICE_DATA_PORST

mcdsl_return_et mcdsrl_open_trace_file(const mcd_char_t* trace_file_name);
// Can be used for on-chip and continuous trace modes
// Use alternatively to mcdsrl_set_trace_buf() for on-chip trace modes
// trace_file will be opened for writing. It will be closed when the recording has stopped.

mcdsl_return_et mcdsrl_set_trace_buf(uint8_t* trace_buf, uint32_t max_trace_size_kb);
// Use alternatively to mcdsrl_open_trace_file() in case of on-chip trace modes
// max_trace_size_kb size must be a multiple of a 4 kByte MCDS paragraph
// Not needed for continuous trace, use mcdsrl_get_data_from_fifo() instead

mcdsl_return_et mcdsrl_set_mcdsconfig(const mcdsl_mcdsc_st* mcdsc, const mcdsl_mcdsc_st* mcdsc_cleanup);
// Pre-condition is that a device is connected. 
// It will be partly checked, whether the device configuration fits to the connected device.
// mcdsc is just stored and will be written to the device in mcdsrl_start_recording(). 
// mcdsc_cleanup is handled similar and will be used in mcdsrl_stop_recording(). 

mcdsl_return_et mcdsrl_start_recording(const mcdsrl_trace_state_st *trace_state_stop_at);
// Trace will be always output starting at the beginning of trace_buf/trace_file
// If currently trace is recorded, this is stopped first.
// Device will be started from Halt After Reset (HAR) state if mcdsc->crsv is MCDSL_CRSV_HAR
// Device configuration will be written to the device
// trace_state_stop_at defines optional one or more conditions for automatically stopping the trace recording.
// trace_state_stop_at is only considered in CONTINUOUS mode. If not needed trace_state_stop_at can be NULL.

mcdsl_return_et mcdsrl_get_recstate(mcdsrl_recstate_et* recstate, mcdsrl_trace_state_st* trace_state);
// recstate will be IDLE when finished and UNKNOWN in case of errors (e.g. device or tool HW connection lost)
// The value of trace_state.trace_size_kb is not influenced by calls of mcdsrl_get_data_from_fifo().
// The value of trace_state.trace_size_kb allows to show a progress bar to the user.

mcdsl_return_et mcdsrl_get_data_from_fifo(uint8_t* trace_buf, uint32_t trace_size_kb_max, uint32_t* trace_size_kb);
// For CONTINUOUS trace mode to get the trace data from the FIFO buffer.
// For IFTG mode, with A1G and A2G, gaps in the trace stream will be marked with an <endoftrace> message. 
// The trace continues after the gap at the following paragraph start.
// trace_size_kb is the amount of trace read from the fifo buffer.
// The implementation of this function will ensure a buffering of at least 2MB of trace data in the fifo.

mcdsl_return_et mcdsrl_stop_recording(mcdsrl_trace_state_st* trace_state);
// Will stop the trace recording if still running and close the trace file if not already closed.
// mcdsc_cleanup is applied to bring the trace resources to a state similar to reset state.
// Has to be called in any case even if the current trace recording has already stopped.

#ifdef __cplusplus
}
#endif

#endif // #define __mcds_lib_recorder_h_


