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
 * MODULE:  mcds_lib_configurator.h
 ******************************************************************************
 DESCRIPTION:
 MCDS configurator library

 ******************************************************************************/

#ifndef __mcds_lib_configurator_h_
#define __mcds_lib_configurator_h_

// This is a C interface, not a C++ interface.
#ifdef __cplusplus
extern "C" {
#endif

#include "mcd_types.h"
#include "mcds_control.h"

#include "mcds_lib.h"

// Includes for mcdscl_get_init_device_type()
#include "mcd_api.h"  // For mcd_core_con_info_st
#include <string.h>  


//-------------------------------------------------------------------------------------------------
#define MCDSCL_COPYRIGHT_STRING "(c) Infineon Technologies AG 2025"
#define MCDSCL_NAME             "MCDS Configurator Library"

#define MCDSCL_VERSION_MAJOR 3  // 210308
#define MCDSCL_VERSION_MINOR 5  // 250403
#define MCDSCL_VERSION_PATCH 1  // 250709
// Version numbers will be read by mcds_configurator_dll.cmake

/*
	Changes V3.5 to V3.5.1 (250709)
	1. IFSD - Ensured now for all device families that there are no IP END/START messages around IFSD gap

	Changes V3.4 to V3.5 (250403)
	1. New TC4x devices supported
	2. Minor bug fixes

	Changes V3.3 to V3.4 (240208)
	1. Added feature to delay trigger trace recording
	2. TC4Dx: Added workaround for issue with new multick message

	Changes V3.2 to V3.3 (230504)
	1. Added register names for TC49xA and TC4Dx 

	Changes V3.1 to V3.2 (221215)
	1. Added TC4x OTGB support
	2. Added TC4D support

	Changes V3.0 to V3.1 (210317)
	1. Timestamps are now before instead of after their triggering event (e.g. data access)
	2. Timestamps now supported for miniMCDS

	Changes V2.1 to V3.0
	1. Added mcdscl_get_active_opoints()
	2. Added mcdscl_set_tg(), replacing mcdscl_set_tg() and mcdscl_set_tg_ocds_sus
	3. Supports TC49x A-step

	Changes V2.0 to V2.1
	1. TC39x A-step not supported anymore
	2. mcdscl_dtu_ctrl_tq_st changed for A3G devices
	   - Replaced bmch_uint by busma_sub
	   - Removed bmch
	   - Added vm and prs
	   - Added utility function mcdscl_dtu_ctrl_st_init()
	4. mcdscl_ptu_ctrl_st added vmeft for A3G devices
	5. mcdsl_mcdsc_hdr_st changed (prepared for supporting different variants in future)
	6. Added mcdscl_set_dtu_exclude_csa() and get function

	Changes V1.0 to V2.0
	1. Added mcdsl_mcdsc_st** mcdsc_cleanup for mcdscl_get_mcdsconfig()
	2. Moved reset control to mcdsl_mcdsc_st* mcdsc to simplify the recorder
	3. Added mcdsl_crsv_et crsv in mcdscl_get_mcdsconfig() and mcdscl_save_mcdsc_file()
		mcdsl_crsv_et contains now the reset control information
	4. Added mcdscl_set_mcdsconfig()
	5. Added mcdscl_load_mcdsc_file()
	6. Added mcdscl_get_init_device_type()
	7. All *stdswtask* strings in names were changed to *statecdtn*
*/

//-------------------------------------------------------------------------------------------------
#define MCDSCL_PTU_MAX_NUM_AR_IP 2  // Only 2->3 out of 6 used for qualification. One for triggering.
typedef struct {
	mctrl_pobptm_et      pobptm;   // Trace mode: instruction, flow, function
	uint8_t              vmeft;    // VMs excluded from trace. One per bit. If 0 all included. Not for A1G, A2G
	mctrl_tgact_et       tgact;    // Trigger action
	mctrl_statecdtn_et   statecdtn_ip;
	mctrl_statecdtn_et   statecdtn_ip_tg;
	mctrl_addr_range_st  ar_ip[MCDSCL_PTU_MAX_NUM_AR_IP];  // Trace qualification
	mctrl_addr_range_st  ar_ip_tg;  // Trigger
} mcdscl_ptu_ctrl_st;
// Note: vmeft is controlling program and data trace if opoint is MCTRL_OPOINT_CPUx (CPU pipeline)

//-------------------------------------------------------------------------------------------------
// DTU trace qualification control
#define MCDSCL_DTU_MAX_NUM_AR_DATA 2  // Only 2 possible for A1G PTU with flow trace sync
typedef struct {
	mctrl_statecdtn_et   statecdtn;
	mctrl_obdtm_et       obdtm;      // Combinations of address, read and write data
	mctrl_dtubmq_et      dtubmq;     // Bus master restriction control
	mctrl_busma_et       busma;      // Bus master
	int16_t              busma_sub;  // If >= 0 this defines the bus master subchannel (mctrl_bmsub_et or DMA channel number)
	int8_t               vm;         // If 0 <= vm <= 7 the trace is restricted to this VM. Not for A1G, A2G
	int8_t               prs;        // If 0 <= prs <= 7 the trace is restricted to this PRS. Not for A1G, A2G
	mctrl_rqdop_et       rqdop;      // Control data trace optional by program qualifier. Setting must be consistent with ar_data.ctrl
	mctrl_addr_range_st  ar_data[MCDSCL_DTU_MAX_NUM_AR_DATA];
} mcdscl_dtu_ctrl_tq_st;
// Note: vm and prs are ignored if the opoint is not an SRI, SPB, etc. bus observation opoint.
// In case of MCTRL_OPOINT_CPUx (CPU pipeline) mcdscl_ptu_ctrl_st.vmeft is effective also for data trace.
// Use mcdscl_dtu_ctrl_st_init()

//-------------------------------------------------------------------------------------------------
// DTU trigger control
typedef struct {
	mctrl_statecdtn_et   statecdtn;
	mctrl_range_et       ip_trig_condition;
	mctrl_tgact_et       tgact;
	mctrl_addr_range_st  ar;
	mctrl_data_range_st  dr;
	mctrl_acctp_et       acctp;  // Access type (R, W or both)
} mcdscl_dtu_ctrl_trig_st;

//-------------------------------------------------------------------------------------------------
// DTU control
typedef struct {
	mcdscl_dtu_ctrl_tq_st   tq;
	mcdscl_dtu_ctrl_trig_st trig;
} mcdscl_dtu_ctrl_st;

inline void mcdscl_dtu_ctrl_st_init(mcdscl_dtu_ctrl_st* dts) {
	*dts = {};
	dts->tq.busma_sub = -1;
	dts->tq.vm        = -1;
	dts->tq.prs       = -1;
}

//-------------------------------------------------------------------------------------------------
typedef enum _mcdscl_tg_type_et {
	MCDSCL_TGT_RESET    = 0,  // No param
	MCDSCL_TGT_OCDS_SUS = 1,  // No param
	MCDSCL_TGT_BUS_ERR  = 2,  // No param
} mcdscl_tg_type_et;


//-------------------------------------------------------------------------------------------------
mcdsl_return_et mcdscl_lib_init(uint8_t version_major);  // Version major ensures compatibility

const mcdsl_version_st* mcdscl_lib_version();

const mcd_char_t* mcdscl_get_error();
// Returns the error string of the last error. NULL in case of no error.

mcdsl_return_et mcdscl_init(uint32_t device_type);
// Returns SUCCESS || PARAM_VALUE_NOT_SUPPORTED
// PARAM_VALUE_NOT_SUPPORTED if the device is not supported
// Initializes the MCDS configuration to the MCDS reset value and sets opoints to DISABLED.
// Note: TC29x (miniMCDS) and TC29xED (MCDS) have the same JTAG ID (device_type)
// For setting TC29x with miniMCDS, please use MCDS_DEVICE_TYPE_A1G_MM as device_type
inline uint32_t mcdscl_get_init_device_type(const mcd_core_con_info_st* con)
{
	return (strncmp(con->device, "TC29x", 7) == 0) ? MCDS_DEVICE_TYPE_A1G_MM : con->device_type;
}

mcdsl_return_et mcdscl_set_recbuf(const mctrl_recbuf_ctrl_st *recbuf_ctrl);
// Returns SUCCESS || FUNCTION_USED_WRONGLY
// Actual configuration could be different due to HW restrictions -> use get function

mcdsl_return_et mcdscl_get_recbuf(mctrl_recbuf_ctrl_st *recbuf_ctrl);
// Returns SUCCESS || FUNCTION_USED_WRONGLY
// Note can be used to identify miniMCDS (MCTRL_RECBUF_TRAM)

mcdsl_return_et mcdscl_set_recbuf_with_delay(const mctrl_recbuf_ctrl_st* recbuf_ctrl, const mctrl_trg_delay_ctrl_st* delay_ctrl);
// Returns SUCCESS || FUNCTION_USED_WRONGLY
// Actual configuration could be different due to HW restrictions -> use get function

mcdsl_return_et mcdscl_get_recbuf_with_delay(mctrl_recbuf_ctrl_st* recbuf_ctrl, mctrl_trg_delay_ctrl_st* delay_ctrl);
// Returns SUCCESS || FUNCTION_USED_WRONGLY
// Note can be used to identify miniMCDS (MCTRL_RECBUF_TRAM)

mcdsl_return_et mcdscl_set_trg_delay(const mctrl_trg_delay_ctrl_st* delay_ctrl);
// Returns SUCCESS || FUNCTION_USED_WRONGLY

mcdsl_return_et mcdscl_get_trg_delay(mctrl_trg_delay_ctrl_st* delay_ctrl);
// Returns SUCCESS || FUNCTION_USED_WRONGLY

mcdsl_return_et mcdscl_set_mcx_time(mctrl_time_et ctrl_time);
// Returns SUCCESS || FUNCTION_USED_WRONGLY

mcdsl_return_et mcdscl_get_mcx_time(mctrl_time_et *ctrl_time);
// Returns SUCCESS || FUNCTION_USED_WRONGLY

mcdsl_return_et mcdscl_set_tg(mcdscl_tg_type_et tg_type, mctrl_tgact_et tgact);
// Only MCTRL_TGACT_TRACE_REC and MCTRL_TGACT_WATCHPOINT are allowed for tgact
// Returns SUCCESS || FUNCTION_USED_WRONGLY || PARAM_VALUE_NOT_SUPPORTED

mcdsl_return_et mcdscl_get_tg(mcdscl_tg_type_et tg_type, mctrl_tgact_et* tgact);
// Returns SUCCESS || FUNCTION_USED_WRONGLY || PARAM_VALUE_NOT_SUPPORTED

mcdsl_return_et mcdscl_setup_opoint(mctrl_opoint_et opoint);
// Returns SUCCESS || FUNCTION_USED_WRONGLY  || FUNCTION_FAILED || PARAM_VALUE_NOT_SUPPORTED
// FUNCTION_USED_WRONGLY if this opoint is already setup
// FUNCTION_FAILED if no HW resource is available (e.g. used for other opoints)
// PARAM_VALUE_NOT_SUPPORTED if the device does not have this opoint

mcdsl_return_et mcdscl_release_opoint(mctrl_opoint_et opoint);
// Returns SUCCESS || FUNCTION_USED_WRONGLY

uint32_t mcdscl_get_active_opoints(const mctrl_opoint_et *opoint_array[]);
// Returns the number of active opoints
// opoint_array can be NULL if not needed

mcdsl_return_et mcdscl_set_dcu(mctrl_opoint_et opoint, mctrl_dienc_et status_trace);
// Returns SUCCESS || FUNCTION_USED_WRONGLY

mcdsl_return_et mcdscl_get_dcu(mctrl_opoint_et opoint, mctrl_dienc_et *status_trace);
// Returns SUCCESS || FUNCTION_USED_WRONGLY

mcdsl_return_et mcdscl_set_dtu_ctrl(mctrl_opoint_et opoint, const mcdscl_dtu_ctrl_st *dtu_ctrl);
// Returns SUCCESS || FUNCTION_USED_WRONGLY
// Actual configuration could be different due to HW restrictions -> use get function

mcdsl_return_et mcdscl_get_dtu_ctrl(mctrl_opoint_et opoint, mcdscl_dtu_ctrl_st *dtu_ctrl);
// Returns SUCCESS || FUNCTION_USED_WRONGLY

mcdsl_return_et mcdscl_set_dtu_exclude_csa(mctrl_opoint_et opoint, const mctrl_addr_range_st *range);
// Returns SUCCESS || FUNCTION_USED_WRONGLY || PARAM_VALUE_NOT_SUPPORTED
// For CPU opoints to exclude the CSA accesses from data trace
// range->ctrl needs to be set to MCTRL_RANGE_OO for excluding CSA. All other values include CSA.
// For TC2xx and TC3xx the address range needs to be defined. TC4xx has hardware support for excluding CSA.

mcdsl_return_et mcdscl_get_dtu_exclude_csa(mctrl_opoint_et opoint, mctrl_addr_range_st *range);
// Returns SUCCESS || FUNCTION_USED_WRONGLY || PARAM_VALUE_NOT_SUPPORTED

mcdsl_return_et mcdscl_set_ptu_ctrl(mctrl_opoint_et opoint, const mcdscl_ptu_ctrl_st *ptu_ctrl);
// Returns SUCCESS || FUNCTION_USED_WRONGLY
// Actual configuration could be different due to HW restrictions -> use get function

mcdsl_return_et mcdscl_get_ptu_ctrl(mctrl_opoint_et opoint, mcdscl_ptu_ctrl_st *ptu_ctrl);
// Returns SUCCESS || FUNCTION_USED_WRONGLY

mcdsl_return_et mcdscl_set_otgb_ctrl(const mctrl_otgb_ctrl_st* otgb_ctrl);
// Returns SUCCESS || FUNCTION_USED_WRONGLY

mcdsl_return_et mcdscl_get_otgb_ctrl(mctrl_otgb_ctrl_st* otgb_ctrl);
// Returns SUCCESS || FUNCTION_USED_WRONGLY

mcdsl_return_et mcdscl_get_reg(uint32_t addr, uint32_t *value);
// Returns SUCCESS || FUNCTION_USED_WRONGLY || MCDSL_RET_FUNCTION_FAILED
// Success if addr is valid and *value is valid

mcdsl_return_et mcdscl_set_reg(uint32_t addr, uint32_t value);
// Returns SUCCESS || FUNCTION_USED_WRONGLY || MCDSL_RET_FUNCTION_FAILED
// Success if addr is valid and value was set
// Only for expert users, since mixed configurations can have unintended side effects!

mcdsl_return_et mcdscl_get_mcdsconfig(mcdsl_crsv_et crsv, const mcdsl_mcdsc_st** mcdsc, const mcdsl_mcdsc_st** mcdsc_cleanup);
// Returns SUCCESS || FUNCTION_USED_WRONGLY
// Can use crsv = MCDSL_CRSV_HOT and mcdsc_cleanup = NULL for decoder
// Get the configuration. The data is stable until the next call.
// mcdsc_cleanup allows to bring the trace resources to a state similar to reset state.
// Always use mcdsc and mcdsc_cleanup from the same call.

mcdsl_return_et mcdscl_set_mcdsconfig(const mcdsl_mcdsc_st* mcdsc);
// Returns SUCCESS || FUNCTION_USED_WRONGLY
// Set the configuration. 
// WARNING: If mcdsc was not created by this library, it is unlikely that the get calls of this library
// return reliable results.
// Main use case is to get mcdsc_cleanup with a following mcdscl_get_mcdsconfig() call.
// This will likely work also for configurations not created by this library.

mcdsl_return_et mcdscl_save_mcdsc_file(mcdsl_crsv_et crsv, const mcd_char_t* mcdsc_file_name);
// Returns SUCCESS || FUNCTION_USED_WRONGLY || COULD_NOT_OPEN_MCDSC_FILE
// Save the configuration as .mcdsc file

mcdsl_return_et mcdscl_load_mcdsc_file(const mcd_char_t* mcdsc_file_name);
// Returns SUCCESS || FUNCTION_USED_WRONGLY || COULD_NOT_OPEN_MCDSC_FILE
// Load the configuration from a .mcdsc or .mcds file
// Main use case is to get mcdsc_cleanup with a following mcdscl_get_mcdsconfig() call

mcdsl_return_et mcdscl_load_json_file(const mcd_char_t* json_file_name);
// Returns SUCCESS || FUNCTION_USED_WRONGLY || COULD_NOT_OPEN_JSON_FILE
// Load the configuration from a .json file
// Main use case is to get mcdsc_cleanup with a following mcdscl_get_mcdsconfig() call

#ifdef __cplusplus
}
#endif

#endif // #define __mcds_lib_configurator_h_


