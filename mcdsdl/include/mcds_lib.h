/*****************************************************************************
 *
 * Copyright (C) 2018-2021 Infineon Technologies AG. All rights reserved.
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
 * MODULE:  mcds_lib.h
 ******************************************************************************
 DESCRIPTION:
 MCDS library (decoder, recorder, configurator) support

 ******************************************************************************/

#ifndef __mcds_lib_h_
#define __mcds_lib_h_

 // This is a C interface, not a C++ interface.
#ifdef __cplusplus
extern "C" {
#endif

#include "mcd_types.h"
#include <memory.h>
#include <assert.h>

//-------------------------------------------------------------------------------------------------
// Common error encoding scheme for all MCDS libraries
typedef enum _mcdsl_return_et {

	MCDSL_RET_SUCCESS = 0x00000000,

	// Events on bits [3:0], exclusively by mcds_lib_recorder.h functions
	MCDSL_RET_DEVICE_PORST              = 0x00000001, // Power-on reset of device detected but function was successful

	MCDSL_RET_FUNCTION_FAILED           = 0x00000010, // Function was not successful (e.g. could not claim a resource)

	// Special error codes used exclusively by mcds_lib_recorder.h functions
	MCDSL_RET_DEVICE_DATA = MCDSL_RET_FUNCTION_FAILED, // Device connection alive but internal access error 
	MCDSL_RET_DEVICE_DATA_PORST = MCDSL_RET_DEVICE_DATA | MCDSL_RET_DEVICE_PORST,  // Only allowed event and error combination
	MCDSL_RET_DEVICE_CONNECTION         = 0x00000100, // Device connection is not available (anymore)
	MCDSL_RET_TOOL_HW_CONNECTION        = 0x00000300, // Tool HW (and thus device) connection is not available (anymore)

	// Special error codes used exclusively by mcds_lib_decoder.h functions
	MCDSL_RET_TRACE_DECODING_ERRORS     = 0x00010000, // Indicates that trace data decodes with errors
	MCDSL_RET_ERROR_ELF_FILE            = 0x00100000, // File does not exist or content is invalid or incomplete
	MCDSL_RET_ERROR_TRACE_FILE          = 0x00400000, // File does not exist or content is invalid or incomplete (binary or .mcds file)

	// Special error code used exclusively by mcds_lib_decoder.h and mcds_lib_recorder.h functions
	MCDSL_RET_ERROR_MCDSC_FILE          = 0x00200000, // File does not exist or content is invalid or incomplete (for this device) 
	MCDSL_RET_ERROR_JSON_FILE          	= 0x00200000, // File does not exist or content is invalid or incomplete (for this device) 

	// Error codes which should only occur during the development phase of the higher layer
	MCDSL_RET_FUNCTION_USED_WRONGLY     = 0x01000000, // Function called e.g. at a wrong point or without preconditions fulfilled
	MCDSL_RET_FUNCTION_NOT_SUPPORTED    = 0x10000000, // This library implementation doesn't support this function
	MCDSL_RET_PARAM_VALUE_NOT_SUPPORTED = 0x20000000, // An (allowed) parameter value is not supported (by this lib implementation)
	MCDSL_RET_UNKNOWN_ERROR             = 0x80000000, // Implementation specific error code cannot be mapped
} mcdsl_return_et;

#define MCDSL_RET_NO_ERROR_STR                  NULL
#define MCDSL_RET_UNKOWN_ERROR_STR              "Unknown internal error"
#define MCDSL_RET_FUNCTION_NOT_SUPPORTED_STR    "Function is not supported by library"
#define MCDSL_RET_PARAM_VALUE_NOT_SUPPORTED_STR "Parameter value is not supported by library"
#define MCDSL_RET_TRACE_DECODING_ERRORS_STR     "Trace file decodes with errors"
#define MCDSL_RET_ERROR_NO_DEVICE_STR           "No device connected"  // Typcal reason for MCDSL_RET_FUNCTION_USED_WRONGLY
#define MCDSL_RET_TOOL_HW_CONNECTION_STR        "Tool HW connection lost"
#define MCDSL_RET_DEVICE_CONNECTION_STR         "Device connection lost"
#define MCDSL_RET_DEVICE_PORST_STR              "Power-on reset (PORST) detected"
#define MCDSL_RET_DEVICE_DATA_PORST_STR         "Data access failed and power-on reset (PORST) detected"

//-------------------------------------------------------------------------------------------------
inline void MCDSL_ASSERT(mcdsl_return_et ret)
{
	assert(ret == MCDSL_RET_SUCCESS);
}

//-------------------------------------------------------------------------------------------------
typedef struct {
	uint8_t  major;
	uint8_t  minor;
	uint16_t patch;
} mcdsl_version_struct_st;

typedef struct {
	union {
		mcdsl_version_struct_st  v;
		uint32_t         version32;  // For easy comparison
	};
} mcdsl_version_st;

//-------------------------------------------------------------------------------------------------
// Config register write sequence variants

// Please note the register write sequence assumes that MCDS and other trace recources are  
// in reset state or at least a cleanup was done after the last configuration.

typedef enum _mcdsl_crsv_et {

	MCDSL_CRSV_HOT       = 1,  
	// Trace recording start for a running device. 
	// Assumes that the clock of used OTGB sources is already on.

	MCDSL_CRSV_HAR       = 2,  
	// Trace and CPU0 start for a device in Halt After Reset (HAR) state. 
	// Enables the clock of used OTGB sources (protection of CLC regs is still off).

	MCDSL_CRSV_NO_START  = 4,
	// Similar to MCDSL_CRSV_HOT but without starting the trace recording
	// This can be used for a flexible start under debugger control

	MCDSL_CRSV_NO_FRILLS = 6,
	// Similar to MCDSL_CRSV_NO_START but without
	// 1. Trace enabling steps in CBS
	// 2. Clock enabling of trace modules
	// 3. Kernel reset of trace modules
	// 4. Trace memory configuration
	// 5. Trace memory RAM initialization
	// This can be used for an embedded trace system control
	// This option is also used for the  part in .mcds files

	MCDSL_CRSV_DAPEA2G   = 9,
	// Similar to MCDSL_CRSV_HOT but omits all SoC reg writes since not accessible.

	MCDSL_CRSV_CLEANUP  = 0xC,
	// Associated cleanup sequence.
} mcdsl_crsv_et;

//-------------------------------------------------------------------------------------------------
// Data struct for exchanging configuration data between configurator, recorder and decoder
// The sequence of addr value pairs is just written to the target by the recorder
#define MCDSL_MCDSC_MAX_NUM_REG 1500  // Sufficient for all TC4xx MCDS instances and TRIF
typedef struct {
	uint8_t  mcdsc_hdr_type; // Type of mcdsl_mcdsc_hdr_st
	uint8_t  reserved8[3];   // Set to 0
	uint32_t mcds_device;    // mcds_device_type_t (reflects MCDS IP type)
	uint32_t device_type;    // Device type  (JTAG ID)
	uint32_t crsv;           // mcdsl_crsv_et (hot attach, after HAR, etc.)
	uint32_t reserved32[2];  // Set to 0
	uint32_t num_reg;        // Number of addr value pairs in two separate arrays
	uint32_t vsi;            // Values start index after the addr part in common uint32_t array
} mcdsl_mcdsc_hdr_st;

typedef struct {
	mcdsl_mcdsc_hdr_st hdr;
	uint32_t d[2*MCDSL_MCDSC_MAX_NUM_REG];
} mcdsl_mcdsc_st;

inline uint32_t mcdsl_mcdsc_size(const mcdsl_mcdsc_st* mcdsc) {
	return (uint32_t)sizeof(mcdsl_mcdsc_hdr_st) + ((mcdsc->hdr.vsi + mcdsc->hdr.num_reg) * (uint32_t)sizeof(uint32_t));
}

inline uint32_t mcdsl_mcdsc_size_compact(const mcdsl_mcdsc_st* mcdsc) {
	return (uint32_t)sizeof(mcdsl_mcdsc_hdr_st) + (2 * mcdsc->hdr.num_reg * (uint32_t)sizeof(uint32_t));
}

inline mcd_bool_t mcdsl_mcdsc_compare(const mcdsl_mcdsc_st* mcdsc0, const mcdsl_mcdsc_st* mcdsc1)
{
	int hdrCmpSize = (int)(&mcdsc0->hdr.vsi - &mcdsc0->hdr.mcds_device);  // Header except vsi
	if (memcmp(&mcdsc0->hdr, &mcdsc1->hdr, hdrCmpSize) != 0)
		return FALSE;
	if (memcmp(mcdsc0->d, mcdsc1->d, mcdsc0->hdr.num_reg * sizeof(uint32_t)) != 0)
		return FALSE;
	if (memcmp(&mcdsc0->d[mcdsc0->hdr.vsi], &mcdsc1->d[mcdsc1->hdr.vsi], mcdsc0->hdr.num_reg * sizeof(uint32_t)) != 0)
		return FALSE;
	return TRUE;
}

inline void mcdsl_mcdsc_copy(mcdsl_mcdsc_st* dst, const mcdsl_mcdsc_st* src)
{
	memcpy(&dst->hdr, &src->hdr, sizeof(mcdsl_mcdsc_hdr_st));
	memcpy(dst->d, src->d, src->hdr.num_reg * sizeof(uint32_t));
	dst->hdr.vsi = src->hdr.num_reg;  // Compact representation
	memcpy(&dst->d[dst->hdr.vsi], &src->d[src->hdr.vsi], src->hdr.num_reg * sizeof(uint32_t));
}

inline uint32_t mcdsl_mcdsc_compactize(mcdsl_mcdsc_st* mcdsc)
{
	memcpy(&mcdsc->d[mcdsc->hdr.num_reg], &mcdsc->d[mcdsc->hdr.vsi], mcdsc->hdr.num_reg * sizeof(uint32_t));
	mcdsc->hdr.vsi = mcdsc->hdr.num_reg;  // Compact representation
	return mcdsl_mcdsc_size(mcdsc);
}


#ifdef __cplusplus
}
#endif

#endif // #define __mcds_lib_decoder_h_


