/*****************************************************************************
 *
 * Copyright (C) 2014-2022 Infineon Technologies AG. All rights reserved.
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
 * MODULE:  mcds_control.h
 ******************************************************************************
 DESCRIPTION:
 Common MCDS control related declarations and definitions

 ******************************************************************************/

#ifndef __mcds_control_h_
#define __mcds_control_h_

 // This is a C interface, not a C++ interface.
#ifdef __cplusplus
extern "C" {
#endif

#include "mcd_types.h"
#include "mcds_ifx_devices_tools.h"

//-------------------------------------------------------------------------------------------------
// MCDS module version
typedef enum mcds_version_et : uint16_t {
	MCDS_VERSION_ERROR       = 0x0000,  // Device has no MCDS

	MCDS_VERSION_MAJOR_MASK  = 0xFF00,
	MCDS_VERSION_CONFIG_MASK = 0x00F0,  // MCDS configuration (e.g. miniMCDS)
	MCDS_VERSION_MACONF_MASK = 0xFFF0,
	MCDS_VERSION_MINOR_MASK  = 0x000F,

	MCDS_VERSION_CONFIG_ED   = 0x0010,
	MCDS_VERSION_CONFIG_MM   = 0x0020,

	MCDS_VERSION_A1G         = 0x0300,  // A1G TC2xx
	MCDS_VERSION_A1G_ED      = 0x0310,  // A1G TC2xx EDs 
	MCDS_VERSION_A1G_MM      = 0x0320,  // A1G TC29x miniMCDS 
	MCDS_VERSION_A2G         = 0x0400,  // A2G TC3xx
	MCDS_VERSION_A2G_ED      = 0x0410,  // A2G TC3xx EDs (MCDS and MCDSlight)
	MCDS_VERSION_A2G_MM      = 0x0420,  // A2G TC3xx miniMCDS 
	MCDS_VERSION_A3G         = 0x0500,  // A3G TC4xx (MCDS4P, MCDS2P)
} mcds_version_et;

mcds_version_et mcds_get_version(uint32_t device_type, bool is_minimcds = false);

inline bool mcds_version_is_minimcds(mcds_version_et mcds_version)
{
	return ((mcds_version == MCDS_VERSION_A1G_MM) || (mcds_version == MCDS_VERSION_A2G_MM));
}

inline uint32_t mcds_get_paragraph_size(mcds_version_et mcds_version)
{
	return mcds_version_is_minimcds(mcds_version) ? 1024 : 4096;
}


//-------------------------------------------------------------------------------------------------
// Generic types used as alternative to midt_device_type_t
typedef enum _mcds_device_type_t {   // Note version nibble [31:28] not used!
	MCDS_DEVICE_TYPE_UNKNOWN = 0x0BADBAD0,
	MCDS_DEVICE_TYPE_A1G_ED  = 0x0A00ED83,
	MCDS_DEVICE_TYPE_A1G_MM  = 0x0A00DD83,
	MCDS_DEVICE_TYPE_A2G_ED  = 0x0A20ED83,  // Starting with TC39x B-step (not A-step)
	MCDS_DEVICE_TYPE_A2G_MM  = 0x0A20DD83,  // Starting with TC39x B-step (not A-step)
	MCDS_DEVICE_TYPE_A3G     = 0x0A30ED83,  // TC4xx 
} mcds_device_type_t;

const char* mcds_device_type_string(uint32_t device_type, bool is_a1g_ed = false);

//-------------------------------------------------------------------------------------------------
typedef enum _mctrl_trace_data_format_et {
	MCTRL_TDF_TDR  = 0,     // A1G, A2G HW Trace Data Raw (TDR) format
	MCTRL_TDF_MTSC = 0x10,  // A3G HW Multi-Trace Stream Container (MTSC) format
	MCTRL_TDF_KEEP = 0x80,  // Keep used trace format
	MCTRL_TDF_ERR  = 0xEE,  // No valid format
} mctrl_trace_data_format_et;

//-------------------------------------------------------------------------------------------------
typedef struct {
	const char*       name;
	uint32_t          device_type;   // midt_device_type_t or mcds_device_type_t
	uint16_t          num_cpus;      // Number of CPUs
	uint16_t          num_pobs;      // Number of POBs
	uint16_t          trace_buf_kb;  // Size of dedicated trace buffer (TBUF, XTM, TRAM)
	midt_aurix_gen_t  aurix_gen;
	mcds_version_et   mcds_version;
	mctrl_trace_data_format_et  trace_data_format;
	uint32_t          addr_cbs;      // Address Cerberus
	uint32_t          addr_mcds;     // Address MCDS, MCDSlight, miniMCDS, MCDS4P+MCDS2P+TRIF
	uint32_t          addr_olda;     // Address OLDA start
	uint32_t          addr_olda_end; // Address OLDA end
} mcds_device_des_t;

//-------------------------------------------------------------------------------------------------
const mcds_device_des_t* mcds_get_device_des(uint32_t device_type);
// A device_type can be ambiguous concerning the device descriptions: with MCDS, miniMCDS or no trace
// This function returns the description with the most powerful trace features.
// Use mcds_get_device_des_minimcds() or mcds_get_device_des_a1g_pd() to get the other descriptions.

const mcds_device_des_t* mcds_get_device_des_a1g_pd(uint32_t device_type);
// For a ambiguous A1G device_type the PD variant without trace is returned

const mcds_device_des_t* mcds_get_device_des_minimcds(uint32_t device_type);
// Returns a NULL pointer if this device has no miniMCDS or is unknown

//-------------------------------------------------------------------------------------------------
// User Interface String - One Hot Selection - Address/Data Range Qualification
#define UIS_OH_RQUAL_DISABLED   "No Address Qualification"
#define UIS_OH_RQUAL_IN_RANGE   "In Range Qualification"
#define UIS_OH_RQUAL_OO_RANGE   "Out Of Range Qualification"

typedef enum _mctrl_range_et {
	MCTRL_RANGE_DISABLED = 0,
	MCTRL_RANGE_IN       = 1,
	MCTRL_RANGE_OO       = 2,
} mctrl_range_et;

const char* mctrl_rqual_get_string(mctrl_range_et rqual);

mctrl_range_et mctrl_rqual_get_enum(const char* text);

//-------------------------------------------------------------------------------------------------
// User Interface String - One Hot Selection - "Read Code from Device"
#define UIS_OH_RCFD_HEADLINE      "Read Code from Device:"
#define UIS_OH_RCFD_WHEN_SUSP     "When device is suspended"
#define UIS_OH_RCFD_IF_NOT_IN_ELF "When not in .elf file"
#define UIS_OH_RCFD_ALWAYS        "Always"
#define UIS_OH_RCFD_NEVER         "Never"

typedef enum {         // Read Reference Data from Device:
	MCTRL_RCFD_WHEN_SUSP,      // When device is suspended
	MCTRL_RCFD_IF_NOT_IN_ELF,  // When not in ELF file
	MCTRL_RCFD_ALWAYS,         // Always
	MCTRL_RCFD_NEVER           // Never
} mctrl_rcfd_et;

//-------------------------------------------------------------------------------------------------
// User Interface String - One Hot Selection - Address Range Qualification DTU Of POB (RQDOP)
#define UIS_OH_RQDOP_DISABLED  "No Address Qualification"
#define UIS_OH_RQDOP_IN_RANGE  "In Range Qualification"
#define UIS_OH_RQDOP_OO_RANGE  "Out Of Range Qualification"
#define UIS_OH_RQDOP_PQ_ONLY   "By Program Qualification"
#define UIS_OH_RQDOP_PQ_OR_IN  "In Range or PQ"
#define UIS_OH_RQDOP_PQ_OR_OO  "Out of Range or PQ"
#define UIS_OH_RQDOP_COMPLEX   "Complex"

typedef enum _mctrl_rqdop_et {
	MCTRL_RQDOP_DISABLED = 0x00,  // Also for the case that the POB DTU is not used for the CPU pipeline
	MCTRL_RQDOP_IN       = 0x01,
	MCTRL_RQDOP_OO       = 0x02,
	MCTRL_RQDOP_PQ_ONLY  = 0x10,
	MCTRL_RQDOP_PQ_OR_IN = 0x11,
	MCTRL_RQDOP_PQ_OR_OO = 0x12,
	MCTRL_RQDOP_COMPLEX  = 0xFF
} mctrl_rqdop_et;

const char* mctrl_rqdop_get_string(mctrl_rqdop_et rqdop);

mctrl_rqdop_et mctrl_rqdop_get_enum(const char* text);

mctrl_range_et mctrl_rqdop_get_range(mctrl_rqdop_et rqdop);

//-------------------------------------------------------------------------------------------------
typedef struct {
	mctrl_range_et ctrl;
	uint32_t       start;
	uint32_t       end;
} mctrl_addr_range_st;

//-------------------------------------------------------------------------------------------------
typedef enum _mctrl_drep_et {  // Data value representation to user
	MCTRL_DREP_HEX = 0,
	MCTRL_DREP_DEC = 1,
} mctrl_drep_et;

typedef struct {
	mctrl_range_et ctrl;
	mctrl_drep_et  drep;
	uint8_t        sign_bit;  // 0 means unsigned
	uint64_t       start;
	uint64_t       end;
	uint64_t       mask;
} mctrl_data_range_st;

//-------------------------------------------------------------------------------------------------
void mctrl_data_comp_string_scanf(const char* comp_str, uint8_t comp_width, mctrl_data_range_st *data_range);
void mctrl_data_comp_string_printf(char* comp_str, uint8_t comp_width, const mctrl_data_range_st *data_range,
	uint8_t str_len = 127, uint8_t line_len = 127);
void mctrl_data_comp_string_test();  // Call only in _DEBUG version


//-------------------------------------------------------------------------------------------------
typedef struct {
	uint8_t   valid;     // If 0 no task is represented by this struct
	uint8_t   cpu;       // CPU index
	uint8_t   vm;        // Virtual Machine (VM) index
	uint8_t   vm_valid;  // If 0 no VM
	uint32_t  id;        // Task ID
} mctrl_stdsw_task_st;

//-------------------------------------------------------------------------------------------------
bool mctrl_stdsw_task_string_scanf(const char* task_str, mctrl_stdsw_task_st *task);

//-------------------------------------------------------------------------------------------------
// User Interface String - One Hot Selection - Disabled/Enabled/Complex
#define UIS_OH_DIENC_DISABLED  "Disabled"
#define UIS_OH_DIENC_ENABLED   "Enabled"
#define UIS_OH_DIENC_COMPLEX   "Complex"

typedef enum _mctrl_dienc_et {
	MCTRL_DIENC_DISABLED = 0,
	MCTRL_DIENC_ENABLED  = 1,
	MCTRL_DIENC_COMPLEX  = 255,
} mctrl_dienc_et;

const char* mctrl_dienc_get_string(mctrl_dienc_et dienc);

mctrl_dienc_et mctrl_dienc_get_enum(const char* text);

//-------------------------------------------------------------------------------------------------
// User Interface String - One Hot Selection - Access Types
#define UIS_OH_ACCTP_ALL      "All Accesses"
#define UIS_OH_ACCTP_READ     "Only Read Accesses"
#define UIS_OH_ACCTP_WRITE    "Only Write Accesses"
#define UIS_OH_ACCTP_COMPLEX  "Complex"

typedef enum _mctrl_acctp_et : uint8_t {
	MCTRL_ACCTP_ALL     = 0,
	MCTRL_ACCTP_READ    = 1,
	MCTRL_ACCTP_WRITE   = 2,
	MCTRL_ACCTP_COMPLEX = 255,
} mctrl_acctp_et;

const char* mctrl_acctp_get_string(mctrl_acctp_et acctp);

mctrl_acctp_et mctrl_acctp_get_enum(const char* text);

//-------------------------------------------------------------------------------------------------
// User Interface String - One Hot Selection - "On-Chip Trace Buffer Mode"
#define UIS_OH_RECBUF_HEADLINE   "Trace Buffer Mode"
#define UIS_OH_RECBUF_STOP_FULL  "Record until full"   // Default selection in GUI
#define UIS_OH_RECBUF_CIRCULAR   "Circular stopped by trigger" 
#define UIS_OH_RECBUF_FIFO_IFTG  "FIFO - if full trace gaps"
#define UIS_OH_RECBUF_FIFO_IFSD  "FIFO - if full suspend device"
#define UIS_OH_RECBUF_COMPLEX    "Complex"

typedef enum _mctrl_recbuf_et {
	MCTRL_RECBUF_STOP_FULL = 0,  // Record until full
	MCTRL_RECBUF_CIRCULAR  = 1,  // Circular stopped by trigger
	MCTRL_RECBUF_FIFO_IFTG = 2,  // FIFO - if full trace gaps
	MCTRL_RECBUF_FIFO_IFSD = 3,  // FIFO - if full suspend device
	MCTRL_RECBUF_COMPLEX = 255,  // Complex
} mctrl_recbuf_et;

const char* mctrl_recbuf_get_string(mctrl_recbuf_et recbuf);

mctrl_recbuf_et mctrl_recbuf_get_enum(const char* text);

inline bool mctrl_recbuf_is_fifo(mctrl_recbuf_et recbuf) {
	switch (recbuf) {
	case MCTRL_RECBUF_FIFO_IFTG:
	case MCTRL_RECBUF_FIFO_IFSD: return true;
	case MCTRL_RECBUF_STOP_FULL:
	case MCTRL_RECBUF_CIRCULAR:
	case MCTRL_RECBUF_COMPLEX:   return false;
	default: assert(false);      return false;
	}
}

//-------------------------------------------------------------------------------------------------
// User Interface String - One Hot Selection - "Delay Trigger Trace Recording"

#define UIS_OH_DELAYED_TRG_HEADLINE "Delay Trigger Trace Recording"
#define UIS_OH_DELAYED_TRG_DIS      "Disabled"
#define UIS_OH_DELAYED_TRG_CLK      "Delayed by MCDS ticks"
#define UIS_OH_DELAYED_TRG_CNT      "Delayed by trigger hits"

typedef enum _mctrl_delayed_trg_et : uint16_t {
	MCTRL_DELAYED_TRG_DIS = 0,		// Disabled
	MCTRL_DELAYED_TRG_CLK = 1,		// Delayed by MCDS ticks
	MCTRL_DELAYED_TRG_CNT = 2,		// Delayed by trigger hits
} mctrl_delayed_trg_et;

typedef struct {
	mctrl_delayed_trg_et mctrl_delay;	// Delay type
	uint32_t delay;						// Delay value
} mctrl_trg_delay_ctrl_st;

const char* mctrl_delay_get_string(mctrl_delayed_trg_et delay);
mctrl_delayed_trg_et mctrl_delay_get_enum(const char* text);

//-------------------------------------------------------------------------------------------------
// User Interface String - One Hot Selection - "Trigger Trace Position"
#define UIS_OH_TRITRA_HEADLINE   "Trigger Trace Position"
#define UIS_OH_TRITRA_START      "Start"
#define UIS_OH_TRITRA_10         "10%"
#define UIS_OH_TRITRA_30         "30%"
#define UIS_OH_TRITRA_60         "60%"
#define UIS_OH_TRITRA_90         "90%"
#define UIS_OH_TRITRA_END        "End" 
#define UIS_OH_TRITRA_COMPLEX    "Complex" 

typedef enum _mctrl_tritra_et {
	MCTRL_TRITRA_START   =   0,
	MCTRL_TRITRA_10      =  10,
	MCTRL_TRITRA_30      =  30,
	MCTRL_TRITRA_60      =  60,
	MCTRL_TRITRA_90      =  90,
	MCTRL_TRITRA_END     = 100,
	MCTRL_TRITRA_COMPLEX = 255,
} mctrl_tritra_et;

const char* mctrl_tritra_get_string(mctrl_tritra_et tritra);

mctrl_tritra_et mctrl_tritra_get_enum(const char* text);


//-------------------------------------------------------------------------------------------------
typedef enum _mctrl_recbuf_mem_type_et {
	MCTRL_RECBUF_UNDEFINED = 0,  // Can be used as wildcard for letting the implemention choose
	MCTRL_RECBUF_TCM       = 1,  // A1G, A2G: TCM tiles in EMEM
	MCTRL_RECBUF_XTM       = 2,  // A1G, A2G: EMEM XTM
	MCTRL_RECBUF_TRAM      = 3,  // A1G, A2G: miniMCDS TRAM
	MCTRL_RECBUF_TBUF      = 4,  // A3G: TBUF in MCDS2P and MCDS4P
	MCTRL_RECBUF_RAM       = 8   // A3G: Any memory that can be written by TME
} mctrl_recbuf_mem_type_et;

const char* mctrl_recbuf_mem_type_get_string(mctrl_recbuf_mem_type_et recbuf_mem_type);

inline bool mctrl_recbuf_mem_type_is_emem(mctrl_recbuf_mem_type_et recbuf_mem_type) {
	switch (recbuf_mem_type) {
	case MCTRL_RECBUF_TCM:
	case MCTRL_RECBUF_XTM:   return true;
	case MCTRL_RECBUF_TRAM:
	case MCTRL_RECBUF_TBUF:
	case MCTRL_RECBUF_RAM:   return false;
	default: assert(false);  return false;
	}
}

//-------------------------------------------------------------------------------------------------
typedef struct {
	uint32_t         addr_kb_start;  // [kByte]
	uint32_t         size_kb;        // [kByte]
	uint32_t         base_addr;      // Base address, required only for MCTRL_RECBUF_RAM
	mctrl_recbuf_et  mctrl_recbuf;
	mctrl_tritra_et  mctrl_tritra;
	mctrl_recbuf_mem_type_et mem_type;
} mctrl_recbuf_ctrl_st;

//-------------------------------------------------------------------------------------------------
// User Interface String - One Hot Selection - Ticks/Timestamps 
#define UIS_OH_TIME_HEADLINE     "Ticks/Timestamps"
#define UIS_OH_TIME_DISABLED     "Disabled"
#define UIS_OH_TIME_TICKS        "Ticks On"
#define UIS_OH_TIME_R_AND_TICKS  "Ticks and Timestamps"
#define UIS_OH_TIME_BY_TRIGGERS  "Timestamps by Triggers"
#define UIS_OH_TIME_COMPLEX      "Complex"

typedef enum _mctrl_time_et {
	MCTRL_TIME_DISABLED = 0,
	MCTRL_TIME_TICKS = 8,
	MCTRL_TIME_R_AND_TICKS = 9,
	MCTRL_TIME_BY_TRIGGERS = 20,
	MCTRL_TIME_COMPLEX = 255,
} mctrl_time_et;

const char* mctrl_time_get_string(mctrl_time_et ctrl_time);

mctrl_time_et mctrl_time_get_enum(const char* text);

//-------------------------------------------------------------------------------------------------
// User Interface String - One Hot Selection - Range Comparison
#define UIS_OH_RCOMP_DISABLED  "No Comparison"
#define UIS_OH_RCOMP_IN_RANGE  "In Range Comparison"
#define UIS_OH_RCOMP_OO_RANGE  "Out of Range Comparison"

const char* mctrl_rcomp_get_string(mctrl_range_et rcomp);

mctrl_range_et mctrl_rcomp_get_enum(const char* text);

//-------------------------------------------------------------------------------------------------
// User Interface String - One Hot Selection - "OCDS Suspend Trigger"
#define UIS_OH_TG_HEADLINE_OCDS_SUS "OCDS Suspend Trigger"
#define UIS_OH_TG_HEADLINE_SPB_ERR  "SPB Error Trigger"
#define UIS_OH_TG_HEADLINE_SRI_ERR  "SRI Error Trigger"
#define UIS_OH_TG_HEADLINE_BUS_ERR  "Bus Error Trigger"
//-------------------------------------------------------------------------------------------------
// User Interface String - One Hot Selection - Trigger Action
#define UIS_OH_TGACT_DISABLED   "Disabled"
#define UIS_OH_TGACT_TRACE_REC  "Trigger Trace Recording"
#define UIS_OH_TGACT_WATCHPOINT "Watchpoint"
#define UIS_OH_TGACT_WP_WITH_TS "Watchpoint with Timestamp" 
#define UIS_OH_TGACT_TIMESTAMP  "Timestamp only"
#define UIS_OH_TGACT_COND_4DT   "Condition for Data Trigger"
#define UIS_OH_TGACT_COMPLEX    "Complex"  // Inactive

typedef enum _mctrl_tgact_et {
	MCTRL_TGACT_DISABLED   = 0,  // No Trigger Action
	MCTRL_TGACT_TRACE_REC  = 2,  // Trigger Trace in addition to watchpoint
	MCTRL_TGACT_WATCHPOINT = 4,  // Watchpoint
	MCTRL_TGACT_WP_WITH_TS = 5,  // Only for PTU and DTU trigger
	MCTRL_TGACT_TIMESTAMP  = 6,  // Only for PTU and DTU trigger
	MCTRL_TGACT_COND_4DT   = 8,  // Only for PTU trigger
	MCTRL_TGACT_COMPLEX    = 255,
} mctrl_tgact_et;

const char* mctrl_tgact_get_string(mctrl_tgact_et tgact);

mctrl_tgact_et mctrl_tgact_get_enum(const char* text);

//-------------------------------------------------------------------------------------------------
// User Interface String - One Hot Selection - SW State Qualifier/Trigger
#define UIS_OH_STATECDTN_NOCO    "No State Condition"
#define UIS_OH_STATECDTN_IN      "State active"
#define UIS_OH_STATECDTN_OO      "State inactive"
#define UIS_OH_STATECDTN_COMPLEX "Complex"

typedef enum _mctrl_statecdtn_et {
	MCTRL_STATECDTN_NOCO = 0,
	MCTRL_STATECDTN_IN = 1,
	MCTRL_STATECDTN_OO = 2,
	MCTRL_STATECDTN_COMPLEX = 255,
} mctrl_statecdtn_et;

const char* mctrl_statecdtn_get_string(mctrl_statecdtn_et statecdtn);

mctrl_statecdtn_et mctrl_statecdtn_get_enum(const char* text);


//-------------------------------------------------------------------------------------------------
// User Interface String - One Hot Selection - DTU Bus Master (Subchannel) Qualification
#define UIS_OH_DTUBMQ_DISABLED  "All Bus Masters"
#define UIS_OH_DTUBMQ_ONLY      "Restrict to Bus Master"
#define UIS_OH_DTUBMQ_EXCLUDE   "Exclude Bus Master"

typedef enum _mctrl_dtubmq_et {
	MCTRL_DTUBMQ_DISABLED = 0,
	MCTRL_DTUBMQ_ONLY     = 1,
	MCTRL_DTUBMQ_EXCLUDE  = 2,
} mctrl_dtubmq_et;

const char* mctrl_dtubmq_get_string(mctrl_dtubmq_et dtubmq);

mctrl_dtubmq_et mctrl_dtubmq_get_enum(const char* text);

//-------------------------------------------------------------------------------------------------
typedef enum _mctrl_msg_type_et {
	MCTRL_MT_UNKNOWN = 0,
	MCTRL_MT_ENDOFTRACE         = 1,   // <end_of_trace>
	MCTRL_MT_IFTG               = 4,   // IFTG trace gap marker (<end_of_trace> for A1G, A2G, MTSC generated for A3G)
	MCTRL_MT_IGNORE             = 9,   // E.g. <skip> message (<skip> is removed in high level trace)
	MCTRL_MT_ERROR_MSG_FIFO     = 10,  // Error in message FIFO (messages dropped)
	MCTRL_MT_ERROR_MSG_FIFO_PTU = 11,  // Error in PTU message FIFO (messages dropped)
	MCTRL_MT_ERROR_MSG_OTHER    = 13,  // Message is e.g. compressed (removed in high level trace)
	MCTRL_MT_DATA               = 20,  // All DTx messages except for MCTRL_MT_OTGB
	MCTRL_MT_WATCH              = 21,  // WPS, WPM watchpoint. Also used as IFSD marker
	MCTRL_MT_COUNTER            = 22,  // EVC (MCX counter message)
	MCTRL_MT_TIME               = 23,  // TSA, TSR (absolute and relative time stamp messages)
	MCTRL_MT_STATUS             = 24,  // DST status messages
	MCTRL_MT_OTGB               = 27,  // DTW messages for opoint OTGB
	MCTRL_MT_PTU                = 30   // Program trace messages (mctrl_msg_subtype_ptu_et)
} mctrl_msg_type_et;

//-------------------------------------------------------------------------------------------------
typedef enum _mctrl_msg_subtype_ptu_et {
	MCTRL_MSTP_UNKNOWN    = 0,
	MCTRL_MSTP_SEQUENCE   = 1,
	MCTRL_MSTP_CFT_CALL   = 5,
	MCTRL_MSTP_CFT_RET    = 6,
	MCTRL_MSTP_CFT_SAMPLE = 7 // Start of trace or end of trace
} mctrl_msg_subtype_ptu_et;

inline bool mctrl_mstp_is_cft(mctrl_msg_subtype_ptu_et msg_subtype)
{
	return ((msg_subtype == MCTRL_MSTP_CFT_CALL)
		|| (msg_subtype == MCTRL_MSTP_CFT_RET)
		|| (msg_subtype == MCTRL_MSTP_CFT_SAMPLE));
}

//-------------------------------------------------------------------------------------------------
// User Interface String - One Hot Selection - "Processor Observation Block" - "Trace Source"
#define UIS_OH_OPOINT_HEADLINE  "Opoint"
#define UIS_OH_OPOINT_DISABLED  "Disabled"
#define UIS_OH_OPOINT_MCDS      "MCDS"
#define UIS_OH_OPOINT_OTGB      "OTGB"
#define UIS_OH_OPOINT_OTGBM     "OTGBM"
#define UIS_OH_OPOINT_DMA       "DMA"
#define UIS_OH_OPOINT_DMA0M0    "DMA0.M0"
#define UIS_OH_OPOINT_DMA0M1    "DMA0.M1"
#define UIS_OH_OPOINT_DMA1M0    "DMA1.M0"
#define UIS_OH_OPOINT_DMA1M1    "DMA1.M1"
#define UIS_OH_OPOINT_RDMA      "RDMA"
#define UIS_OH_OPOINT_SPU0      "SPU0"
#define UIS_OH_OPOINT_SPU1      "SPU1"
#define UIS_OH_OPOINT_RIF       "RIF"
#define UIS_OH_OPOINT_PPU       "PPU"
#define UIS_OH_OPOINT_PPUIIM    "PPU.IIM"
#define UIS_OH_OPOINT_SPB       "SPB"
#define UIS_OH_OPOINT_CPB       "CPB"
#define UIS_OH_OPOINT_COMPB     "COMPB"
#define UIS_OH_OPOINT_OLDA      "OLDA"
#define UIS_OH_OPOINT_CPU0      "CPU0"
#define UIS_OH_OPOINT_CPU1      "CPU1"
#define UIS_OH_OPOINT_CPU2      "CPU2"
#define UIS_OH_OPOINT_CPU3      "CPU3"
#define UIS_OH_OPOINT_CPU4      "CPU4"
#define UIS_OH_OPOINT_CPU5      "CPU5"
#define UIS_OH_OPOINT_CPUCS     "CPUcs"
#define UIS_OH_OPOINT_SRICPU0   "SRI.CPU0"
#define UIS_OH_OPOINT_SRICPU1   "SRI.CPU1"
#define UIS_OH_OPOINT_SRICPU2   "SRI.CPU2"
#define UIS_OH_OPOINT_SRICPU3   "SRI.CPU3"
#define UIS_OH_OPOINT_SRICPU4   "SRI.CPU4"
#define UIS_OH_OPOINT_SRICPU5   "SRI.CPU5"
#define UIS_OH_OPOINT_SRICPUCS  "SRI.CPUcs"
#define UIS_OH_OPOINT_IIMCPU0   "CPU0.IIM"
#define UIS_OH_OPOINT_IIMCPU1   "CPU1.IIM"
#define UIS_OH_OPOINT_IIMCPU2   "CPU2.IIM"
#define UIS_OH_OPOINT_IIMCPU3   "CPU3.IIM"
#define UIS_OH_OPOINT_IIMCPU4   "CPU4.IIM"
#define UIS_OH_OPOINT_IIMCPU5   "CPU5.IIM"
#define UIS_OH_OPOINT_IIMCPUCS  "CPUcs.IIM"
#define UIS_OH_OPOINT_PMU_PF0   "SRI.PMU.PF0" 
#define UIS_OH_OPOINT_PMU_PF01  "SRI.PMU.PF01"
#define UIS_OH_OPOINT_PMU_PF1   "SRI.PMU.PF1" 
#define UIS_OH_OPOINT_PMU_PF23  "SRI.PMU.PF23"
#define UIS_OH_OPOINT_PMU_DF    "SRI.PMU.DF" 
#define UIS_OH_OPOINT_XBAR      "SRI.XBAR"
#define UIS_OH_OPOINT_SRI_LMU   "SRI.LMU"  
#define UIS_OH_OPOINT_SRI       "SRI"
#define UIS_OH_OPOINT_LMU0      "LMU0"
#define UIS_OH_OPOINT_COMPLEX   "Complex"


typedef enum _mctrl_opoint_et : uint8_t {
	MCTRL_OPOINT_UNKNOWN  = 0,
	MCTRL_OPOINT_DISABLED = 0,

	MCTRL_OPOINT_MCDS     = 0x10,  // A123
	MCTRL_OPOINT_OTGB     = 0x14,  // A123
	MCTRL_OPOINT_OTGBM    = 0x16,  // A-23

	MCTRL_OPOINT_DMA      = 0x90,  // A12-
	MCTRL_OPOINT_DMA0M0   = 0x91,  // A--3
	MCTRL_OPOINT_DMA0M1   = 0x92,  // A--3
	MCTRL_OPOINT_DMA1M0   = 0x93,  // A--3
	MCTRL_OPOINT_DMA1M1   = 0x94,  // A--3
	MCTRL_OPOINT_RDMA     = 0x9A,  // A--3

	MCTRL_OPOINT_SPU0     = 0xA0,  // A12-
	MCTRL_OPOINT_SPU1     = 0xA1,  // A12-
	MCTRL_OPOINT_RIF      = 0xA4,  // A--3
	MCTRL_OPOINT_PPU      = 0xA8,  // A--3
	MCTRL_OPOINT_PPUIIM   = 0xA9,  // A--3

	MCTRL_OPOINT_SPB      = 0xB0,  // A123
	MCTRL_OPOINT_CPB      = 0xB1,  // A--3
	MCTRL_OPOINT_COMPB    = 0xB2,  // A--3
	MCTRL_OPOINT_OLDA     = 0xBF,  // A123

	MCTRL_OPOINT_CPU0     = 0xC0,  // A123
	MCTRL_OPOINT_CPU1     = 0xC1,  // A123
	MCTRL_OPOINT_CPU2     = 0xC2,  // A123
	MCTRL_OPOINT_CPU3     = 0xC3,  // A123
	MCTRL_OPOINT_CPU4     = 0xC4,  // A123
	MCTRL_OPOINT_CPU5     = 0xC5,  // A123
	MCTRL_OPOINT_CPUCS    = 0xCF,  // A123
	MCTRL_OPOINT_SRICPU0  = 0xD0,  // A-23
	MCTRL_OPOINT_SRICPU1  = 0xD1,  // A-23
	MCTRL_OPOINT_SRICPU2  = 0xD2,  // A-23
	MCTRL_OPOINT_SRICPU3  = 0xD3,  // A-23
	MCTRL_OPOINT_SRICPU4  = 0xD4,  // A-23
	MCTRL_OPOINT_SRICPU5  = 0xD5,  // A-23
	MCTRL_OPOINT_SRICPUCS = 0xDF,  // A-23
	MCTRL_OPOINT_IIMCPU0  = 0xE0,  // A--3
	MCTRL_OPOINT_IIMCPU1  = 0xE1,  // A--3
	MCTRL_OPOINT_IIMCPU2  = 0xE2,  // A--3
	MCTRL_OPOINT_IIMCPU3  = 0xE3,  // A--3
	MCTRL_OPOINT_IIMCPU4  = 0xE4,  // A--3
	MCTRL_OPOINT_IIMCPU5  = 0xE5,  // A--3
	MCTRL_OPOINT_IIMCPUCS = 0xEF,  // A--3

	// Deprecated opoints
	MCTRL_OPOINT_PMU_PF0  = 0xF0,  // A1--
	MCTRL_OPOINT_PMU_PF01 = 0xF1,  // A1--
	MCTRL_OPOINT_PMU_PF1  = 0xF4,  // A1--
	MCTRL_OPOINT_PMU_PF23 = 0xF5,  // A1--
	MCTRL_OPOINT_PMU_DF   = 0xF6,  // A1--
	MCTRL_OPOINT_XBAR     = 0xF7,  // A1--
	MCTRL_OPOINT_SRI_LMU  = 0xF8,  // A1--
	MCTRL_OPOINT_SRI      = 0xF9,  // A12-  Marker for A1G/A2G DCU and WTU messages of BOB_SRI
	MCTRL_OPOINT_LMU0     = 0xFA,  // A-2-

	MCTRL_OPOINT_COMPLEX = 0xFF
} mctrl_opoint_et;

const char* mctrl_opoint_get_string(mctrl_opoint_et opoint);

const char* mctrl_opoint_get_string_for_trace(mctrl_opoint_et opoint);

mctrl_opoint_et mctrl_opoint_get_enum(const char* text);

bool mctrl_opoint_is_cpu(mctrl_opoint_et opoint);
// Returns true for MCTRL_OPOINT_CPUx, MCTRL_OPOINT_SRICPUx and MCTRL_OPOINT_IIMCPUx

bool mctrl_opoint_is_cpu_pipeline(mctrl_opoint_et opoint);
// Returns true for MCTRL_OPOINT_CPUx

mctrl_opoint_et mctrl_get_pobopoint_cpu(mctrl_opoint_et opoint);
// Returns MCTRL_OPOINT_CPUx for MCTRL_OPOINT_CPUx, MCTRL_OPOINT_SRICPUx and MCTRL_OPOINT_IIMCPUx

bool mctrl_opoint_cpu_same_instance(mctrl_opoint_et opoint0, mctrl_opoint_et opoint1);
// Returns true if the opoints are for the same CPU instance (TCMUX only allows to observe one)

bool mctrl_opoint_has_subchannel(mctrl_opoint_et opoint);
// Returns true if the opoint provides data trace with subchannel information (master ID, etc.)

uint8_t mctrl_get_cpu_index(midt_aurix_gen_t aurix_gen, mctrl_opoint_et opoint);

enum { MCTRL_MAX_NUM_OB = 6 };
typedef struct {
	mctrl_opoint_et o[8][MCTRL_MAX_NUM_OB];  // Encoding of TMUX bitfield (starting with A3G)
} mctrl_opoint_matrix_st;

const mctrl_opoint_matrix_st* mctrl_get_opoint_matrix(uint32_t device_type);

//-------------------------------------------------------------------------------------------------
// User Interface String - One Hot Selection - "Bus Masters"
#define UIS_OH_BUSMA_HEADLINE  "Bus Masters"
#define UIS_OH_BUSMA_DISABLED  ""   // Menu selection is just empty
#define UIS_OH_BUSMA_CPU0      "CPU0" 
#define UIS_OH_BUSMA_CPU1      "CPU1" 
#define UIS_OH_BUSMA_CPU2      "CPU2" 
#define UIS_OH_BUSMA_CPU3      "CPU3" 
#define UIS_OH_BUSMA_CPU4      "CPU4" 
#define UIS_OH_BUSMA_CPU5      "CPU5" 
#define UIS_OH_BUSMA_CPUCS     "CPUCS" 
#define UIS_OH_BUSMA_CPU_C0    "All CPUs" 
#define UIS_OH_BUSMA_PPU0      "PPU"
#define UIS_OH_BUSMA_DMA       "DMA"
#define UIS_OH_BUSMA_DMA1      "DMA1"
#define UIS_OH_BUSMA_CBS       "CBS" 
#define UIS_OH_BUSMA_TRIF      "TRIF" 
#define UIS_OH_BUSMA_GETH      "GETH" 
#define UIS_OH_BUSMA_GETH1     "GETH1" 
#define UIS_OH_BUSMA_LETH0     "LETH0" 
#define UIS_OH_BUSMA_LETH1     "LETH1" 
#define UIS_OH_BUSMA_CANXL0    "CANXL" 
#define UIS_OH_BUSMA_HSSL      "HSSL" 
#define UIS_OH_BUSMA_HSSL1     "HSSL1" 
#define UIS_OH_BUSMA_GTM0      "GTM" 
#define UIS_OH_BUSMA_PCIE0     "PCIE0" 
#define UIS_OH_BUSMA_PCIE1     "PCIE1" 
#define UIS_OH_BUSMA_DRET0     "DRE.T0" 
#define UIS_OH_BUSMA_DRET1     "DRE.T1" 
#define UIS_OH_BUSMA_SDMMC     "SDMMC" 
#define UIS_OH_BUSMA_XSPI0     "XSPI" 
#define UIS_OH_BUSMA_CIF       "CIF" 
#define UIS_OH_BUSMA_RIF0      "RIF0" 
#define UIS_OH_BUSMA_RIF1      "RIF1" 
#define UIS_OH_BUSMA_RMEM      "RMEM" 
#define UIS_OH_BUSMA_RDMA      "RDMA" 
#define UIS_OH_BUSMA_AMU       "AMU"
#define UIS_OH_BUSMA_HSM       "HSM"
#define UIS_OH_BUSMA_COMPLEX   "Complex" 

typedef enum _mctrl_busma_et : uint16_t {
	MCTRL_BUSMA_DISABLED = 0,
	MCTRL_BUSMA_CPU0     = 0x0C00,  // A123
	MCTRL_BUSMA_CPU1     = 0x0C01,  // A123
	MCTRL_BUSMA_CPU2     = 0x0C02,  // A123
	MCTRL_BUSMA_CPU3     = 0x0C03,  // A-23
	MCTRL_BUSMA_CPU4     = 0x0C04,  // A-23
	MCTRL_BUSMA_CPU5     = 0x0C05,  // A-23
	MCTRL_BUSMA_CPUCS    = 0x0C0F,  // A--3
	MCTRL_BUSMA_CPU_C0   = 0x0CFF,  // A123  All CPUs (cluster 0)
	MCTRL_BUSMA_PPU0     = 0x1C20,  // A--3
	MCTRL_BUSMA_DMA0     = 0x40D0,  // A123  A2G: also DMA1!
	MCTRL_BUSMA_DMA1     = 0x40D1,  // A--3
	MCTRL_BUSMA_CBS      = 0x4400,  // A123
	MCTRL_BUSMA_TRIF     = 0x4410,  // A--3
	MCTRL_BUSMA_GETH0    = 0x8000,  // A-23
	MCTRL_BUSMA_GETH1    = 0x8001,  // A--3
	MCTRL_BUSMA_LETH0    = 0x8010,  // A--3
	MCTRL_BUSMA_LETH1    = 0x8011,  // A--3
	MCTRL_BUSMA_CANXL0   = 0x8020,  // A--3
	MCTRL_BUSMA_HSSL0    = 0x8040,  // A-23  A2G: also HSSL1!
	MCTRL_BUSMA_HSSL1    = 0x8041,  // A-23
	MCTRL_BUSMA_GTM0     = 0x8080,  // A--3
	MCTRL_BUSMA_PCIE0    = 0x80C0,  // A--3
	MCTRL_BUSMA_PCIE1    = 0x80C1,  // A--3
	MCTRL_BUSMA_DRET0    = 0x8100,  // A--3
	MCTRL_BUSMA_DRET1    = 0x8101,  // A--3
	MCTRL_BUSMA_SDMMC0   = 0x8140,  // A--3
	MCTRL_BUSMA_XSPI0    = 0x8180,  // A--3
	MCTRL_BUSMA_CIF      = 0x8804,  // A12-
	MCTRL_BUSMA_RIF0     = 0x8810,  // A--3  TC45, TC43
	MCTRL_BUSMA_RIF1     = 0x8811,  // A--3  TC45
	MCTRL_BUSMA_RMEM     = 0x8820,  // A--3  TC45, TC43
	MCTRL_BUSMA_RDMA     = 0x8828,  // A--3  TC45, TC43
	MCTRL_BUSMA_AMU      = 0xF802,  // A12-  All instances
	MCTRL_BUSMA_HSM      = 0xF804,  // A-2-  A2G HSM can be traced at SPB
	MCTRL_BUSMA_ERR      = 0xFE00,  // Error master tag
	MCTRL_BUSMA_COMPLEX  = 0xFFFF,
} mctrl_busma_et;

const char* mctrl_busma_get_string(mctrl_busma_et busma);

mctrl_busma_et mctrl_busma_get_enum(const char* text);

//-------------------------------------------------------------------------------------------------
// User Interface String - One Hot Selection - "Bus Master Subchannel"
#define UIS_OH_BMSUB_DISABLED    ""   // Menu selection is just empty
#define UIS_OH_BMSUB_CPU_DMI     "DMI"
#define UIS_OH_BMSUB_CPU_DMISAFE "DMIsafe"
#define UIS_OH_BMSUB_CPU_PMI     "PMI"

typedef enum _mctrl_bmsub_et : uint8_t {
	MCTRL_BMSUB_CPU_DISABLED  = 0,
	MCTRL_BMSUB_CPU_DMI       = 1,
	MCTRL_BMSUB_CPU_DMISAFE   = 2,
	MCTRL_BMSUB_CPU_PMI       = 3,
} mctrl_bmsub_et;

const char* mctrl_bmsub_get_string(mctrl_bmsub_et busma_sub);

mctrl_bmsub_et mctrl_bmsub_get_enum(const char* text);

//-------------------------------------------------------------------------------------------------
// User Interface String - One Hot Selection - POB/BOB - "Data Trace"
#define UIS_OH_OBDTM_HEADLINE  "Data Trace"
#define UIS_OH_OBDTM_DISABLED  "Disabled" 
#define UIS_OH_OBDTM_ADRW      "All" 
#define UIS_OH_OBDTM_A_RW      "A R+W" 
#define UIS_OH_OBDTM_A__W      "A Write" 
#define UIS_OH_OBDTM_A_R       "A Read" 
#define UIS_OH_OBDTM_AD_W      "A+D Write" 
#define UIS_OH_OBDTM_ADR       "A+D Read" 
#define UIS_OH_OBDTM_COMPLEX   "Complex" 

typedef enum _mctrl_obdtm_et {
	MCTRL_OBDTM_DISABLED = 0,
	MCTRL_OBDTM_ADRW     = 0x33,
	MCTRL_OBDTM_A_RW     = 0x23,
	MCTRL_OBDTM_A__W     = 0x21,
	MCTRL_OBDTM_A_R      = 0x22,
	MCTRL_OBDTM_AD_W     = 0x31,
	MCTRL_OBDTM_ADR      = 0x32,
	MCTRL_OBDTM_COMPLEX  = 0xC0,
} mctrl_obdtm_et;

const char* mctrl_obdtm_get_string(mctrl_obdtm_et obdtm);

mctrl_obdtm_et mctrl_obdtm_get_enum(const char* text);

//-------------------------------------------------------------------------------------------------
// User Interface String - One Hot Selection - POB - "Program Trace"
#define UIS_OH_POBPTM_HEADLINE  "Program Trace"
#define UIS_OH_POBPTM_DISABLED  "Disabled" 
#define UIS_OH_POBPTM_INSTR     "Instruction" 
#define UIS_OH_POBPTM_FLOW      "Flow" 
#define UIS_OH_POBPTM_FUNC      "Function" 
#define UIS_OH_POBPTM_COMPLEX   "Complex" 

typedef enum _mctrl_pobptm_et {
	MCTRL_POBPTM_DISABLED = 0,
	MCTRL_POBPTM_INSTR    = 1,
	MCTRL_POBPTM_FLOW     = 2,
	MCTRL_POBPTM_FUNC     = 3,
	MCTRL_POBPTM_COMPLEX  = 0xC0
} mctrl_pobptm_et;

const char* mctrl_pobptm_get_string(mctrl_pobptm_et pobptm);

mctrl_pobptm_et mctrl_pobptm_get_enum(const char* text);

//-------------------------------------------------------------------------------------------------
// User Interface String - One Hot Selection - "OTGB0/1/2 Source"
#define UIS_OH_OTGB_SRC_HEADLINE  "OTGB Source"

#define UIS_OH_OTGB_SRC_DISABLED  "Disabled" 
#define UIS_OH_OTGB_SRC_INT       "INT" 
#define UIS_OH_OTGB_SRC_DMA       "DMA" 
#define UIS_OH_OTGB_SRC_SMU       "SMU" 
#define UIS_OH_OTGB_SRC_ADC       "ADC" 
#define UIS_OH_OTGB_SRC_PMS       "PMS"

#define UIS_OH_OTGB_SRC_ERAY      "ERAY"
#define UIS_OH_OTGB_SRC_CAN       "CAN"
#define UIS_OH_OTGB_SRC_HSSL      "HSSL"
#define UIS_OH_OTGB_SRC_HSCT      "HSCT"
#define UIS_OH_OTGB_SRC_GTM       "GTM"
#define UIS_OH_OTGB_SRC_EGTM      "EGTM"
#define UIS_OH_OTGB_SRC_SBCU      "SBCU"
#define UIS_OH_OTGB_SRC_CBCU      "CBCU"
#define UIS_OH_OTGB_SRC_CSS		  "CSS"
#define UIS_OH_OTGB_SRC_PCIE	  "PCIE"
#define UIS_OH_OTGB_SRC_PPUC	  "PPUC"

#define UIS_OH_OTGB_SRC_COMPLEX   "Complex" 

typedef enum _mctrl_otgb_src_et : uint8_t {
	MCTRL_OTGB_SRC_DISABLED = 0,
	MCTRL_OTGB_SRC_INT      = 1,  // A123
	MCTRL_OTGB_SRC_DMA      = 2,  // A123
	MCTRL_OTGB_SRC_SMU      = 3,  // A123
	MCTRL_OTGB_SRC_ADC      = 4,  // A-23
	MCTRL_OTGB_SRC_PMS      = 5,  // A-23
	
	MCTRL_OTGB_SRC_ERAY     = 7,  // A--3
	MCTRL_OTGB_SRC_CAN      = 8,  // A--3
	MCTRL_OTGB_SRC_HSSL     = 9,  // A--3
	MCTRL_OTGB_SRC_HSCT     = 10, // A--3
	MCTRL_OTGB_SRC_GTM      = 11, // A--3
	MCTRL_OTGB_SRC_EGTM     = 12, // A--3
	MCTRL_OTGB_SRC_SBCU     = 13, // A--3
	MCTRL_OTGB_SRC_CBCU     = 14, // A--3
	MCTRL_OTGB_SRC_CSS      = 15, // A--3
	MCTRL_OTGB_SRC_PCIE     = 16, // A--3
	MCTRL_OTGB_SRC_PPUC     = 17, // A--3

	MCTRL_OTGB_SRC_COMPLEX  = 0xFF,
} mctrl_otgb_src_et;

const char* mctrl_otgb_src_get_string(mctrl_otgb_src_et otgb_src);

const char* mctrl_otgb_src_get_string_for_trace(mctrl_otgb_src_et otgb_src, uint8_t otgb_index);

mctrl_otgb_src_et mctrl_otgb_src_get_enum(const char* text);


//-------------------------------------------------------------------------------------------------
// User Interface String - One Hot Selection - "OTGB0/1 Trace"
#define UIS_OH_OTGB0_HEADLINE  "OTGB0 Trace"
#define UIS_OH_OTGB1_HEADLINE  "OTGB1 Trace"

#define UIS_OH_OTGB_TS16_DISABLED     "Disabled" 
#define UIS_OH_OTGB_TS16_INT_SSI      "INT TS16_SSI" 
#define UIS_OH_OTGB_TS16_INT_SP       "INT TS16_SP" 
#define UIS_OH_OTGB_TS16_INT_IS       "INT TS16_IS"      // TS16 with TS8_IS  as lower byte
#define UIS_OH_OTGB_TS16_INT_SPA      "INT TS16_SPA"     // TS16 with TS8_SPA as lower byte
#define UIS_OH_OTGB_TS16_DMA_PF       "DMA TS16_PF" 
#define UIS_OH_OTGB_TS16_DMA_ERR      "DMA TS16_ERR" 
#define UIS_OH_OTGB_TS16_DMA_C15      "DMA TS16_C15" 
#define UIS_OH_OTGB_TS16_DMA_C31      "DMA TS16_C31" 
#define UIS_OH_OTGB_TS16_DMA_C47      "DMA TS16_C47" 
#define UIS_OH_OTGB_TS16_DMA_C63      "DMA TS16_C63" 
#define UIS_OH_OTGB_TS16_DMA_C79      "DMA TS16_C79" 
#define UIS_OH_OTGB_TS16_DMA_C95      "DMA TS16_C95" 
#define UIS_OH_OTGB_TS16_DMA_C111     "DMA TS16_C111" 
#define UIS_OH_OTGB_TS16_DMA_C127     "DMA TS16_C127" 
#define UIS_OH_OTGB_TS16_SMU_SMU      "SMU TS16_SMU" 
#define UIS_OH_OTGB_TS16_VADC_SSIGC   "VADC TS16_SSIGC" 
#define UIS_OH_OTGB_TS16_VADC_SSIGF   "VADC TS16_SSIGF" 
#define UIS_OH_OTGB_TS16_PMS_ADCMON   "PMS TS16_ADCMON" 
#define UIS_OH_OTGB_TS16_PMS_EVRCON   "PMS TS16_EVRCON" 
#define UIS_OH_OTGB_TS16_PMS_PMS      "PMS TS16_PMS" 
#define UIS_OH_OTGB_TS16_PMS_EVRC     "PMS TS16_EVRC" 
#define UIS_OH_OTGB_TS16_COMPLEX      "Complex" 

#define UIS_OH_OTGB_TS16_TMADC_SOC_TS1		"ADC TS16_TS1"
#define UIS_OH_OTGB_TS16_TMADC_SOC_TS2		"ADC TS16_TS2"
#define UIS_OH_OTGB_TS16_SMU_SAFE0			"SMU TS16_SAFE0"
#define UIS_OH_OTGB_TS16_SMU_SAFE1			"SMU TS16_SAFE1"
#define UIS_OH_OTGB_TS16_SMU_CS				"SMU TS16_CS"
#define UIS_OH_OTGB_TS16_ERAY_SEP			"ERAY TS16_SEP"
#define UIS_OH_OTGB_TS16_ERAY_MC			"ERAY TS16_MC"
#define UIS_OH_OTGB_TS16_CAN				"CAN TS16_CAN"
#define UIS_OH_OTGB_TS16_HSSL_STR			"HSSL TS16_STR"
#define UIS_OH_OTGB_TS16_HSSL_ERR			"HSSL TS16_ERR"
#define UIS_OH_OTGB_TS16_HSCT				"HSCT TS16_HSCT"
#define UIS_OH_OTGB_TS16_GTM_IOS			"GTM TS16_IOS"
#define UIS_OH_OTGB_TS16_GTM_MCSI			"GTM TS16_MCSI" // MCSi/i+1
#define UIS_OH_OTGB_TS16_GTM_MCSE			"GTM TS16_MCSE" // MCS with even cluster number including 0
#define UIS_OH_OTGB_TS16_GTM_MCSO			"GTM TS16_MCSO" // MCS with odd cluster number
//#define UIS_OH_OTGBM_TS32_GTM_MCA			"GTM TS32_MCA"
//#define UIS_OH_OTGBM_TS32_GTM_DRA1A			"GTM TS32_DRA1A"
//#define UIS_OH_OTGBM_TS32_GTM_DRA1BC		"GTM TS32_DRA1BC"
//#define UIS_OH_OTGBM_TS32_GTM_DRA2			"GTM TS32_DRA2"
//#define UIS_OH_OTGBM_TS32_GTM_MCD_PROG		"GTM TS32_MCD_PROG"
//#define UIS_OH_OTGBM_TS32_GTM_MCD_DATA		"GTM TS32_MCD_DATA"
//#define UIS_OH_OTGBM_TS32_GTM_DRD1A			"GTM TS32_DRD1A"
//#define UIS_OH_OTGBM_TS32_GTM_DRD1BC		"GTM TS32_DRD1BC"
//#define UIS_OH_OTGBM_TS32_GTM_DRD2			"GTM TS32_DRD2"
//#define UIS_OH_OTGBM_TS32_GTM_ARU			"GTM TS32_ARU"
//#define UIS_OH_OTGBM_TS32_GTM_TTB0			"GTM TS32_TTB0"
//#define UIS_OH_OTGBM_TS32_GTM_TTB1			"GTM TS32_TTB1"
//#define UIS_OH_OTGBM_TS32_GTM_TTB2			"GTM TS32_TTB2"
//#define UIS_OH_OTGBM_TS32_GTM_TTB3			"GTM TS32_TTB3"
#define UIS_OH_OTGB_TS16_EGTM_IOS			"eGTM TS16_IOS"
#define UIS_OH_OTGB_TS16_BCU		        "BCU TS16_BCU"
#define UIS_OH_OTGB_TS16_CSS				"CSS TS16_CSS"
#define UIS_OH_OTGB_TS16_PCIE_PCIE0			"PCIE TS16_PCIE0"
#define UIS_OH_OTGB_TS16_PCIE_PCIE1			"PCIE TS16_PCIE1"
#define UIS_OH_OTGB_TS16_PCIE_PCIE2			"PCIE TS16_PCIE2"
#define UIS_OH_OTGB_TS16_PPUC				"PPU TS16_PPU"


typedef enum _mctrl_otgb_ts16_et : uint16_t {
	MCTRL_OTGB_TS16_DISABLED			= 0,

	MCTRL_OTGB_TS16_INT_SSI				= 101,  // A12-
	MCTRL_OTGB_TS16_INT_SP				= 102,  // A123
	MCTRL_OTGB_TS16_INT_IS				= 111,  // A123   TS8 for A12
	MCTRL_OTGB_TS16_INT_SPA				= 112,  // A123   TS8 for A12

	MCTRL_OTGB_TS16_DMA_PF				= 201,  // A123
	MCTRL_OTGB_TS16_DMA_ERR				= 202,  // A123
	MCTRL_OTGB_TS16_DMA_C15				= 208,  // A123
	MCTRL_OTGB_TS16_DMA_C31				= 209,  // A123
	MCTRL_OTGB_TS16_DMA_C47				= 210,  // A123
	MCTRL_OTGB_TS16_DMA_C63				= 211,  // A123
	MCTRL_OTGB_TS16_DMA_C79				= 212,  // A123
	MCTRL_OTGB_TS16_DMA_C95				= 213,  // A123
	MCTRL_OTGB_TS16_DMA_C111			= 214,  // A123
	MCTRL_OTGB_TS16_DMA_C127			= 215,  // A123

	MCTRL_OTGB_TS16_SMU_SMU				= 301,  // A12-

	MCTRL_OTGB_TS16_SMU_SAFE0			= 302,	// A--3
	MCTRL_OTGB_TS16_SMU_SAFE1			= 303,	// A--3
	MCTRL_OTGB_TS16_SMU_CS				= 304,	// A--3

	MCTRL_OTGB_TS16_VADC_SSIGC			= 401,  // A12-
	MCTRL_OTGB_TS16_VADC_SSIGF			= 402,  // A12-

	MCTRL_OTGB_TS16_TMADC_SOC_TS1		= 403,	// A--3
	MCTRL_OTGB_TS16_TMADC_SOC_TS2		= 404,	// A--3

	MCTRL_OTGB_TS16_PMS_ADCMON			= 501,  // A-2-
	MCTRL_OTGB_TS16_PMS_EVRCON			= 502,  // A-2-
	MCTRL_OTGB_TS16_PMS_PMS				= 511,  // A--3
	MCTRL_OTGB_TS16_PMS_EVRC			= 512,  // A--3

	MCTRL_OTGB_TS16_ERAY_SEP			= 601,
	MCTRL_OTGB_TS16_ERAY_MC				= 602,

	MCTRL_OTGB_TS16_CAN					= 701,

	MCTRL_OTGB_TS16_HSSL_STR			= 801,
	MCTRL_OTGB_TS16_HSSL_ERR			= 802,

	MCTRL_OTGB_TS16_HSCT				= 901,

	MCTRL_OTGB_TS16_GTM_IOS				= 1001,
	MCTRL_OTGB_TS16_GTM_MCSI			= 1002, // MCSi/i+1
	MCTRL_OTGB_TS16_GTM_MCSE			= 1003, // MCS with even cluster number including 0
	MCTRL_OTGB_TS16_GTM_MCSO			= 1004, // MCS with odd cluster number

	MCTRL_OTGB_TS16_EGTM_IOS			= 1101,

	MCTRL_OTGB_TS16_BCU_PERF_EVTS		= 1201,

	MCTRL_OTGB_TS16_CSS					= 1301,

	MCTRL_OTGB_TS16_PCIE_PCIE0			= 1401,
	MCTRL_OTGB_TS16_PCIE_PCIE1			= 1402,
	MCTRL_OTGB_TS16_PCIE_PCIE2			= 1403,

	MCTRL_OTGB_TS16_PPUC				= 1501,

	//MCTRL_OTGBM_TS32_GTM_MCA			= 1005,
	//MCTRL_OTGBM_TS32_GTM_DRA1A		= 1006,
	//MCTRL_OTGBM_TS32_GTM_DRA1BC		= 1007,
	//MCTRL_OTGBM_TS32_GTM_DRA2			= 1008,
	//MCTRL_OTGBM_TS32_GTM_MCD_PROG		= 1009,
	//MCTRL_OTGBM_TS32_GTM_MCD_DATA		= 1010,
	//MCTRL_OTGBM_TS32_GTM_DRD1A		= 1011,
	//MCTRL_OTGBM_TS32_GTM_DRD1BC		= 1012,
	//MCTRL_OTGBM_TS32_GTM_DRD2			= 1013,
	//MCTRL_OTGBM_TS32_GTM_ARU			= 1014,
	//MCTRL_OTGBM_TS32_GTM_TTB0			= 1015,
	//MCTRL_OTGBM_TS32_GTM_TTB1			= 1016,
	//MCTRL_OTGBM_TS32_GTM_TTB2			= 1017,
	//MCTRL_OTGBM_TS32_GTM_TTB3			= 1018,

	MCTRL_OTGB_TS16_COMPLEX = 0xC000,
} mctrl_otgb_ts16_et;

const char* mctrl_otgb_ts16_get_string(mctrl_otgb_ts16_et otgb_ts16);

mctrl_otgb_ts16_et mctrl_otgb_ts16_get_enum(const char* text);

//-------------------------------------------------------------------------------------------------
const char* mctrl_otgb_get_tos_string_a1g(uint32_t device_type, uint32_t tos);
const char* mctrl_otgb_get_tos_string_a2g(uint32_t tos);
const char* mctrl_otgb_get_tos_string_a3g(uint32_t tos);


//-------------------------------------------------------------------------------------------------
// OTGB control
#define MCTRL_OTGB_MAX_NUM_PARAM  32  // GTM has 16
#define MCTRL_CTRL_OTGB_MAX_NUM_PARAM  2

typedef struct {
	uint32_t irq0;  // INT_OIXS0.IRQ0
	uint32_t irq1;  // INT_OIXS0.IRQ1
	uint32_t irq2;  // INT_OIXS1.IRQ2
	uint32_t irq3;  // INT_OIXS1.IRQ3
	uint32_t mirq;  // INT_OIXMS.MIRQ
} mctrl_otgb_param_int_is_st;

typedef struct {
	uint32_t tos;   // INT_OIT.TOS
} mctrl_otgb_param_int_sp_st;   

typedef struct {
	uint32_t lam;   // PMS_OTSC0.BxLAM
	uint32_t ham;   // PMS_OTSC0.BxHAM
} mctrl_otgb_param_pms_adcmon_st;  

typedef struct {
	uint32_t ec;      // PMS_OTSC1.BxEC
	uint32_t dmonad;  // PMS_OTSC1.DMONAD
	uint32_t smcdbg;  // PMS_OTSC1.SMCDBG
} mctrl_otgb_param_pms_evrcon_st;  

typedef union {
	//uint32_t  value[MCTRL_OTGB_MAX_NUM_PARAM]; // Allocate memory for union
	mctrl_otgb_param_int_is_st int_is;   // TC2x, TC3x
	mctrl_otgb_param_int_sp_st int_sp;   // TC2x, TC3x
	mctrl_otgb_param_pms_adcmon_st pms_adcmon;   // TC3x
	mctrl_otgb_param_pms_evrcon_st pms_evrcon;   // TC3x
} mctrl_otgb_param_a1g2g_st;

typedef struct {
	uint32_t bxpms;	// OTSC0.BxPMS
}  mctrl_otgb_param_pms_pms_st;

typedef struct {
	uint32_t bxevrc;	// OTSC1.BxEVRC
	uint32_t evrcout;	// OTSC1.EVRCOUT
}  mctrl_otgb_param_pms_evrc_st;

typedef struct {
	uint32_t trig1sel;	// OCSTM.TRIG1SEL
	uint32_t trig2sel;	// OCSTM.TRIG2SEL
}  mctrl_otgb_param_tmadc_ts_st;

typedef struct {
	uint32_t tbu0_cm;	// OCDS_OTBU0T.CM
	uint32_t tbu0_cv;	// OCDS_OTBU0T.CV
	uint32_t tbu1_en;	// OCDS_OTBU1T.EN
	uint32_t tbu1_cv;	// OCDS_OTBU1T.CV
	uint32_t tbu2_en;	// OCDS_OTBU2T.EN
	uint32_t tbu2_cv;	// OCDS_OTBU2T.CV
	uint32_t tbu3_en;	// OCDS_OTBU3T.EN
	uint32_t tbu3_cv;	// OCDS_OTBU3T.CV
	uint32_t b0lmt;		// OCDS_OTSC0.B0LMT
	uint32_t b0lmi;		// OCDS_OTSC0.B0LMI
	uint32_t b0hmt;		// OCDS_OTSC0.B0HMT
	uint32_t b0hmi;		// OCDS_OTSC0.B0HMI
	uint32_t b1lmt;		// OCDS_OTSC0.B1LMT
	uint32_t b1lmi;		// OCDS_OTSC0.B1LMI
	uint32_t b1hmt;		// OCDS_OTSC0.B1HMT
	uint32_t b1hmi;		// OCDS_OTSC0.B1HMI
}  mctrl_otgb_param_ios_st;

typedef union {
	uint32_t  value[MCTRL_OTGB_MAX_NUM_PARAM]; // Allocate memory for union
	mctrl_otgb_param_int_is_st int_is;
	mctrl_otgb_param_int_sp_st int_sp;
	mctrl_otgb_param_pms_pms_st pms_pms;
	mctrl_otgb_param_pms_evrc_st pms_evrc;
	mctrl_otgb_param_tmadc_ts_st tmadc_ts;
	mctrl_otgb_param_ios_st gtm_ios;
	mctrl_otgb_param_ios_st egtm_ios;
} mctrl_otgb_param_a3g_st;


typedef struct {  // Parameters of a Trigger Set (TS)
	uint16_t    num_param;
	uint8_t     reset;          // Input: Force params to their reset values
	union {
		mctrl_otgb_param_a1g2g_st a1g2g;
		mctrl_otgb_param_a3g_st a3g;
	};
} mctrl_otgb_param_st;


typedef struct {
	mctrl_otgb_src_et   src;
	uint16_t            instance;
	mctrl_otgb_ts16_et  ts16;
	mctrl_otgb_param_st param;
} mctrl_otgb16_ctrl_st;

typedef struct {
	mctrl_otgb16_ctrl_st  otgb16[MCTRL_CTRL_OTGB_MAX_NUM_PARAM];  // OTGB0/1
} mctrl_otgb_ctrl_st;

inline bool mctrl_otgb_ctrl_is_disabled(const mctrl_otgb_ctrl_st* otgb_ctrl) {
	return ((otgb_ctrl->otgb16[0].src == MCTRL_OTGB_SRC_DISABLED)
		&& (otgb_ctrl->otgb16[1].src == MCTRL_OTGB_SRC_DISABLED));
}

// This utility function assumes an exclusive usage of either OTGB0 or OTGB1 for a specific trigger set
// This is not the case e.g. for the PMS trigger sets
inline uint32_t mcr_otgb_ctrl_find_tgs16(const mctrl_otgb_ctrl_st* oc, mctrl_otgb_src_et otgb_src)
{
	if (oc->otgb16[0].src == otgb_src) {
		if (oc->otgb16[0].ts16 == MCTRL_OTGB_TS16_DISABLED) {
			if ((oc->otgb16[1].src == otgb_src) && 
				(oc->otgb16[1].ts16 != MCTRL_OTGB_TS16_DISABLED)) {
				return 1;
			}
		}
		return 0;
	}
	else if (oc->otgb16[1].src == otgb_src) {
		return 1;
	}
	return 0xFFFF;
}


#ifdef __cplusplus
}
#endif

#endif // __mcds_control_h_
