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
 * MODULE:  mcds_lib_decoder.h
 ******************************************************************************
 DESCRIPTION:
 MCDS decoder library
 
 ******************************************************************************/

#ifndef __mcds_lib_decoder_h_
#define __mcds_lib_decoder_h_

// This is a C interface, not a C++ interface.
#ifdef __cplusplus
extern "C" {
#endif

#include "mcd_types.h"
#include "mcds_control.h"

#include "mcds_lib.h"

//-------------------------------------------------------------------------------------------------
#define MCDSDL_COPYRIGHT_STRING "(c) Infineon Technologies AG 2025"
#define MCDSDL_NAME             "MCDS Decoder Library"


#define MCDSDL_VERSION_MAJOR 4  // 250423
#define MCDSDL_VERSION_MINOR 1  // 250801
#define MCDSDL_VERSION_PATCH 0  // 250801
// Version numbers will be read by mcds_decoder_dll.cmake

/*
	Changes V4.0 to V4.1 (250801)
	1. CMcdsHlDecoder: IPE message handling improved
	2. Issues for "IP END" output for IPE fixed
	3. IPE oper string now IP instead of IPx
	4. Workaround for TC3x MCDS behavior. IPE increment leads to EIAA error after a branch otherwise.
	   Observed for TC37x miniMCDS IFSD trace.
	5. IFSD - Ensured now for all device families that there are no IP END/START messages around IFSD gap
	6. IFSD - Added wctds at the end of mcdsdl_error_counter_st

	Changes V3.8 to V4.0 (250403)
	1. Added function mcdsdl_mcds_file_save()

	Changes V3.7 to V3.8 (250403)
	1. New TC4x devices supported
	2. Minor bug fixes

	Changes V3.6 to V3.7 (240208)
	1. Added feature TAS_PRINT protocol by OLDA trace and text output as comment
	3. Minor bug fix in ELF loader
	3. TC4Dx: Added workaround for issue with new multick message
	4. TC4Dx: Added workarounds for issues with VM state changes

	Changes V3.5 to V3.6 (230504)
	1. Added register names for TC49xA and TC4Dx
	2. Fixed missing initial timestamp issue for post trigger trace mode
	3. Fixed message decoding issues for TC4Dx
	4. Added AUTOSAR ARTI OS trace support

	Changes V3.4 to V3.5 (220120)
	1. Added MCDSDL_DM_RAW as decoding mode for Infineon internal purposes
	2. Added mcdsdl_decode(NULL, 0, MCTRL_TDF_KEEP, dm) to support MCDSDL_DM_RAW for trace files
	3. Added TC4x OTGB support
	4. Added TC4D support

	Changes V3.3 to V3.4
	1. Added mcdsdl_get_code_symbol_addr() and mcdsdl_get_data_symbol_addr()
	2. Register names are returned for all AURIX devices

	Changes V3.2 to V3.3
	1. CPUx.DMI subchannel encoding in sideband is converted from 0 to 3 (TC4xx family encoding) for TC49x A-step
	
	Changes V3.1 to V3.2
	1. sideband_valid provides now the number of valid sideband bits
	2. Added vm to mt_ptu and mt_status
	3. Added MCTRL_MT_OTGB and mt_otgb
	4. Added mcdsdl_get_trace_marker() and removed mcdsdl_get_trigger_trace_msg_index()

	Changes V3.0 to V3.1
	1. Introduced tbuf_index for MCTRL_MT_IFTG to determine the source of the overrun

	Changes V2.0 to V3.0
	1. Added ecthg to mcdsdl_error_counter_st
	2. Added mcdsdl_get_msg_str()
	3. time_r is always positive (not biased anymore by trigger_trace_msg_index)
	4. TC39x A-step not supported anymore
	5. Removed time_a from mcdsdl_msg_st
	6. Added mt_time to mcdsdl_msg_st
	7. Added mcdsdl_set_device_callback()
	8. mcdsl_mcdsc_hdr_st changed (prepared for supporting different variants in future)

	Changes V1.0 to V2.0
	1. Removed ob and opoint_ptu from and added res_dbg to mcdsdl_msg_st
*/

//-------------------------------------------------------------------------------------------------
typedef struct {  
	uint8_t   tbuf_index;       // Index of the trace buffer where the gap occurred 
} mcdsdl_mt_iftg;

typedef struct {  
	uint64_t  time;
	uint8_t   is_tsa;           // Timestamp is from an absolute clock source (MCDS TSA not TSR)
} mcdsdl_mt_time;

typedef struct {  
	uint32_t  value;            // Status data, which is opoint specific (refer to MCDS documentation)
	uint32_t  value_last;       // Value of last STATUS message of this opoint (for detection of edges).
	uint8_t   value_last_valid; // If 0 value_last is invalid
	uint8_t   vm;               // Currently active VM for CPU opoints. 0xFF if not supported or unknown
} mcdsdl_mt_status;

typedef struct {  
	uint32_t  watchpoints;    // Each bit represents a watchpoint. More than one can be active
	uint8_t   trigger_trace;  // Message contains the watchpoint associated with trigger trace
	uint8_t   ifsd;           // Message marks a device suspend phase for MCTRL_RECBUF_FIFO_IFSD
} mcdsdl_mt_watch;

typedef struct {  
	uint64_t  value;  // Counter value
	uint32_t  index;  // Counter index
} mcdsdl_mt_counter;

typedef struct { 
	uint64_t  data;
	uint32_t  addr;
	uint32_t  sideband;       // TC2x, TC3x: subchannel [11:5], master [4:1], supervisor [0]
								// TC4x:                  [12:6]         [5:1]             [0]
	uint8_t   r_not_w;        // If 0 this is a write
	uint8_t   num_bytes;      // 0 or 1/2/4/8 data bytes. O means only address trace
	uint8_t   addr_valid;     // If 0 addr is invalid (addr value is 0 in this case)
	uint8_t   sideband_valid; // Number of valid sideband bits
} mcdsdl_mt_data;

typedef struct { 
	uint16_t  otgb[2];                // OTGB0/1
	uint32_t  otgb2;                  // OTGB2
	uint8_t   otgb2_valid;            // If 0 otgb2 is not valid
	mctrl_otgb_src_et  otgb_src[3];  // Source of OTGB0/1/2
} mcdsdl_mt_otgb;

typedef struct {  // MCTRL_MSTP_SEQUENCE
	uint32_t  first;   // Address of first instruction in linear sequence
	uint32_t  last;    // Address of last instruction in linear sequence
} mcdsdl_mstp_sequence;

typedef struct {  // MCTRL_MSTP_CFT_CALL
	uint32_t  source;  // Address of CALL instruction
	uint32_t  target;  // Target address of CALL (first instruction of called function)
} mcdsdl_mstp_cft_call;

typedef struct {  // MCTRL_MSTP_CFT_RET
	uint32_t  source;  // Address of RET instruction
	uint32_t  target;  // Target address of RET (instruction after associated call instruction)
} mcdsdl_mstp_cft_ret;

typedef struct {  // MCTRL_MSTP_CFT_SAMPLE
	uint32_t  sample;  // Sampled address at the start of (a qualified) CFT trace or at the end of a qualifed CFT trace
} mcdsdl_mstp_cft_sample;

typedef struct { 
	mctrl_msg_subtype_ptu_et  subtype;
	uint8_t   start_of_trace;  // Marks the start of (a qualified) trace
	uint8_t   end_of_trace;    // Marks the end of a qualified trace
	uint8_t   vm;              // Currently active VM. 0xFF if not supported or unknown
	union {
		mcdsdl_mstp_sequence mstp_sequence; 	// MCTRL_MSTP_SEQUENCE
		mcdsdl_mstp_cft_call mstp_cft_call; 	// MCTRL_MSTP_CFT_CALL
		mcdsdl_mstp_cft_ret mstp_cft_ret; 		// MCTRL_MSTP_CFT_RET
		mcdsdl_mstp_cft_sample mstp_cft_sample; // MCTRL_MSTP_CFT_SAMPLE
	};
} mcdsdl_mt_ptu;

typedef struct {
	int64_t             time_r;    // Time in MCDS clock cycles, -1 if unknown
	uint32_t            index;     // 0xFFFFFFFF if invalid
	mctrl_opoint_et    opoint;    // MCDS or DTU opoint in case of POB or BOB
	uint8_t             oblock;    // Internal encoding of the observation block
	mctrl_msg_type_et  msg_type;  // mctrl_msg_type_et
	uint8_t             res_dbg;   // Reserved for decoder and MCDS HW debugging purposes

	// Messages without further data in this struct
					// MCTRL_MT_ENDOFTRACE
					// MCTRL_MT_IFTG
					// MCTRL_MT_ERROR_MSG_FIFO
					// MCTRL_MT_ERROR_MSG_FIFO_PTU
					// MCTRL_MT_ERROR_MSG_OTHER
	union {
		mcdsdl_mt_iftg mt_iftg; 		// MCTRL_MT_IFTG
		mcdsdl_mt_time mt_time; 		// MCTRL_MT_TIME
		mcdsdl_mt_status mt_status; 	// MCTRL_MT_STATUS
		mcdsdl_mt_watch mt_watch; 		// MCTRL_MT_WATCH
		mcdsdl_mt_counter mt_counter; 	// MCTRL_MT_COUNTER
		mcdsdl_mt_data mt_data; 		// MCTRL_MT_DATA
		mcdsdl_mt_otgb mt_otgb; 		// MCTRL_MT_OTGB
		mcdsdl_mt_ptu mt_ptu;			// MCTRL_MT_PTU
	};
} mcdsdl_msg_st;


//-------------------------------------------------------------------------------------------------
typedef enum mcdsdl_flow_instr_type_et {
	MCDSDL_FIT_UNKNOWN = 0,  // Instruction unknown
	MCDSDL_FIT_NONE    = 1,  // Not a flow changing instruction
	MCDSDL_FIT_BRANCH  = 2,  // Jump or loop instruction
	MCDSDL_FIT_JI      = 3,  // Indirect jump
	MCDSDL_FIT_CALL    = 4,  // Call
	MCDSDL_FIT_CALLI   = 5,  // Indirect call
	MCDSDL_FIT_RET     = 6,  // Return
	MCDSDL_FIT_RFE     = 7,  // Return from exception (interrupt, trap service routine)
	MCDSDL_FIT_RFM     = 8,  // Return from debug monitor
	MCDSDL_FIT_RFH     = 9   // Return from hypervisor
} mcdsdl_flow_instr_type_et;

//-------------------------------------------------------------------------------------------------
typedef struct {
	// MCDSDL_MSO_MIN
	uint32_t          index;   // 0xFFFFFFFF if invalid
	const mcd_char_t* time_a;  // Time in [ns]. Set MCDS frequency with mcdsdl_set_mcdsconfig() or with file
	const mcd_char_t* time_r;  // Time in MCDS clock cycles, relative to trigger_trace
	const mcd_char_t* ticks;   // MCDS clock cycles since the last message
	const mcd_char_t* opoint;
	const mcd_char_t* origin;
	const mcd_char_t* operation;
	const mcd_char_t* addr;
	const mcd_char_t* data;
	const mcd_char_t* trace_state;

	// MCDSDL_MSO_SYMBOL
	const mcd_char_t* symbol;
	const mcd_char_t* symbol_offset;

	// MCDSDL_MSO_COMMENT or MCDSDL_MSO_COMMENT_MULTILINE
	const mcd_char_t* comment;

	// MCDSDL_MSO_ADVANCED
	const mcd_char_t* stream_state;  // e.g. stack level, interrupt nesting level, etc.
	const mcd_char_t* cft;           // Compact function trace information

	// MCDSDL_MSO_TDR
	const mcd_char_t* tdr_msg;       // Trace Data Raw message
	const mcd_char_t* tdr_addr;      // Message address in trace data
	const mcd_char_t* tdr_len;       // Message length
	const mcd_char_t* tdr_msg_head;  // First nibbles of the message
} mcdsdl_msg_str_st;

//-------------------------------------------------------------------------------------------------
typedef enum mcdsdl_msg_str_option_et
{
	MCDSDL_MSO_BASIC             = 0x01, 
	MCDSDL_MSO_SYMBOL            = 0x02,
	MCDSDL_MSO_COMMENT           = 0x04,
	MCDSDL_MSO_COMMENT_MULTILINE = 0x0C,
	MCDSDL_MSO_ADVANCED          = 0x20,
	MCDSDL_MSO_TDR               = 0x80,
	MCDSDL_MSO_BSCMAT = MCDSDL_MSO_BASIC | MCDSDL_MSO_SYMBOL | MCDSDL_MSO_COMMENT_MULTILINE | MCDSDL_MSO_ADVANCED | MCDSDL_MSO_TDR,
	MCDSDL_MSO_BSCMA  = MCDSDL_MSO_BASIC | MCDSDL_MSO_SYMBOL | MCDSDL_MSO_COMMENT_MULTILINE | MCDSDL_MSO_ADVANCED,
	MCDSDL_MSO_BSCM   = MCDSDL_MSO_BASIC | MCDSDL_MSO_SYMBOL | MCDSDL_MSO_COMMENT_MULTILINE,
	MCDSDL_MSO_BSC    = MCDSDL_MSO_BASIC | MCDSDL_MSO_SYMBOL | MCDSDL_MSO_COMMENT,
	MCDSDL_MSO_BSCAT  = MCDSDL_MSO_BASIC | MCDSDL_MSO_SYMBOL | MCDSDL_MSO_COMMENT           | MCDSDL_MSO_ADVANCED | MCDSDL_MSO_TDR,
	MCDSDL_MSO_BSCA   = MCDSDL_MSO_BASIC | MCDSDL_MSO_SYMBOL | MCDSDL_MSO_COMMENT           | MCDSDL_MSO_ADVANCED,
	MCDSDL_MSO_BS     = MCDSDL_MSO_BASIC | MCDSDL_MSO_SYMBOL,
} mcdsdl_msg_str_option_et;

//-------------------------------------------------------------------------------------------------
typedef enum mcdsdl_decoding_mode_et {
	MCDSDL_DM_SINGLE = 0x05,  // Decode full trace data at once
	MCDSDL_DM_CONT   = 0x0C,  // Decode but keep final states for the following decoding
	MCDSDL_DM_RAW    = 0xF0,  // Decode without post processing (like MTV Smart Display Mode off)
} mcdsdl_decoding_mode_et;
// CONT mode ensure the removal of  trace artefacts by using overlapping trace analysis.
// First call of mcdsdl_decode() with MCDSDL_DM_CONT after mcdsdl_clear():
//     The decoder is initialized and the trace is decoded and analyzed
// All following calls of mcdsdl_decode() with MCDSDL_DM_CONT:
//     The trace data is decoded considering state information from the last call of mcdsdl_decode()
//     This ensures a clean trace in case of a pure data trace with or without time information.
//     In case of any kind of program trace it can't be guaranteed that there are no artefacts like
//     redundant messages at the boundary between different trace chunks.

//-------------------------------------------------------------------------------------------------
#define MCDSDL_MAX_TRACE_DATA_SIZE_KB  20480  // 20 MB 
// Maximum size of trace_data for mcdsdl_decode()

//-------------------------------------------------------------------------------------------------
typedef struct {
  uint32_t sum_of_all;
  uint32_t wced;
  uint32_t eiaa;
  uint32_t eitm;
  uint32_t edem;
  uint32_t etfo;
  uint32_t etfop;
  uint32_t ecthg;
  uint32_t wctds;
} mcdsdl_error_counter_st;

#define MCDSDL_EC_STR_WCED  "WARNING Need Complete ELF file for Decoding (WCED)"
#define MCDSDL_EC_STR_EIAA  "ERROR Instruction Address Alignment (EIAA)"
#define MCDSDL_EC_STR_EITM  "ERROR Instruction Type Mismatch (EITM)"
#define MCDSDL_EC_STR_EDEM  "ERROR Device ELF file Mismatch (EDEM)"
#define MCDSDL_EC_STR_ETFO  "ERROR Trace message FIFO Overflow (ETFO)"
#define MCDSDL_EC_STR_ETFOP "ERROR Trace message FIFO Overflow Program (ETFOP)"
#define MCDSDL_EC_STR_ECTHG "ERROR Continuous IFTG Trace Has Gaps (ECTHG)"
#define MCDSDL_EC_STR_WCTDS "WARNING Continuous IFSD Trace Device Suspended (WCTDS)"

//-------------------------------------------------------------------------------------------------
typedef struct {
	int32_t num_msg;            // Number of messages available. Same as mcdsdl_get_num_msg()
	int32_t mi_trigger_trace;   // First MCTRL_MT_WATCH msg with trigger_trace, -1 if not found
	int32_t mi_all_mcds_start;  // Start of range with messages from all MCDS instances
	int32_t mi_all_mcds_end;    // End of range with messages from all MCDS instances
	int32_t reserved[12];
} mcdsdl_trace_marker_st;

//-------------------------------------------------------------------------------------------------
mcdsl_return_et mcdsdl_lib_init(uint8_t version_major);  // Version major ensures compatibility

const mcdsl_version_st* mcdsdl_lib_version();

const mcd_char_t* mcdsdl_get_error();  // Returns the error string of the last error

int32_t mcdsdl_get_num_msg();
// Returns 0 if no messages available

int32_t mcdsdl_get_msg(int32_t mi, mcdsdl_msg_st *msg);
// Returns the msg index (mi) in case of success, -1 otherwise.

int32_t mcdsdl_get_next_msg(const mcdsdl_msg_st *msg_ref, mcdsdl_msg_st *msg);
// Returns the msg index in case of success, -1 otherwise.
// msg is the next message with same opoint, opoint_ptu and msg_type
// msg_type is ignored if the reference value is 0 (MCTRL_MT_UNKNOWN)
// msg_ref and msg can be the same object. msg_ref.index is then updated for the next call of this function

int32_t mcdsdl_get_previous_msg(const mcdsdl_msg_st *msg_ref, mcdsdl_msg_st *msg);
// Returns the msg index in case of success, -1 otherwise.
// msg is the previous message with same opoint, opoint_ptu and msg_type
// msg_type is ignored if the reference value is 0 (MCTRL_MT_UNKNOWN)
// msg_ref and msg can be the same object. msg_ref.index is then updated for the next call of this function

uint32_t mcdsdl_get_msg_str(const mcdsdl_msg_st *msg, mcdsdl_msg_str_option_et mso, uint32_t line, mcdsdl_msg_str_st *msg_str);
// Returns the number of lines in the strings, 0 otherwise.
// MCDSDL_MSO_COMMENT_MULTILINE: the parameter line will set the maximum lines output in the comment and related strings. 
// MCDSDL_MSO_COMMENT: The parameter line is the index of the line returned by this call. 

mcdsl_return_et mcdsdl_get_instr(mctrl_opoint_et opoint_ptu, uint32_t addr, uint32_t *instr, uint32_t *instr_size, const mcd_char_t **instr_str);
// Returns SUCCESS || FUNCTION_FAILED
// *instr_size is the instruction width in bytes (4/2 for 32/16 bits)
// FUNCTION_FAILED if this address is not defined in the ELF file (*instr and *instr_size will be 0).
// If instr_str is not NULL *instr_str will point to the disassembler string

mcdsdl_flow_instr_type_et mcdsdl_get_instr_type(uint32_t instr, int32_t *offset, mcd_bool_t *absolute);

mcd_bool_t mcdsdl_get_code_symbol(mctrl_opoint_et opoint_ptu, uint32_t addr, const mcd_char_t** symbol, uint32_t *start_addr, uint32_t *size);
mcd_bool_t mcdsdl_get_data_symbol(mctrl_opoint_et opoint_ptu, uint32_t addr, const mcd_char_t** symbol, uint32_t *start_addr, uint32_t *size);
// Returns TRUE on success
// opoint_ptu can be usually 0 (MCTRL_OPOINT_UNKNOWN), except for addresses in the CPU local segments C and D.
// start_addr of the symbol (0 if information is not available in the ELF file or no known register)
// size of the symbol (0 if information is not available in the ELF file or no known register)

mcd_bool_t mcdsdl_get_code_symbol_addr(mctrl_opoint_et opoint_ptu, const mcd_char_t* symbol, uint32_t* start_addr, uint32_t* size);
mcd_bool_t mcdsdl_get_data_symbol_addr(mctrl_opoint_et opoint_ptu, const mcd_char_t* symbol, uint32_t* start_addr, uint32_t* size);
// Returns TRUE on success
// opoint_ptu can be usually 0 (MCTRL_OPOINT_UNKNOWN), except for symbols in the CPU local segments C and D.
// start_addr of the symbol (0 if information is not available in the ELF file or no known register)
// size of the symbol (0 if information is not available in the ELF file or no known register)


mcdsl_return_et mcdsdl_elf_file_load(const mcd_char_t *elf_file_name, mcd_bool_t add_file);
// Returns SUCCESS || COULD_NOT_OPEN_ELF_FILE || MCDSL_RET_TRACE_DECODING_ERRORS
// It is possible to load several ELF files which cover different address ranges.
// If trace data is already present, it will be decoded again using the loaded ELF file.

void mcdsdl_elf_file_unload_all();

mcdsl_return_et mcdsdl_mcds_file_load(const mcd_char_t *mcds_file_name);
// Returns SUCCESS || COULD_NOT_OPEN_TRACE_FILE || ERROR_READING_TRACE_FILE || DECODING_ERRORS
// The .mcds file provides the device configuration including MCDS and (optional) trace data

mcdsl_return_et mcdsdl_mcds_file_save(const mcd_char_t* mcds_file_name, mcd_bool_t include_symbols_and_code, mcd_bool_t ascii_not_binary);
// Returns SUCCESS || COULD_NOT_OPEN_TRACE_FILE || FUNCTION_USED_WRONGLY  || FUNCTION_FAILED
// Call after mcdsdl_decode(trace_data) when trace_data is still available.

mcdsl_return_et mcdsdl_set_mcdsconfig(const mcdsl_mcdsc_st* mcdsc, float mcds_frequency_hz);
// Returns SUCCESS
// mcds_frequency_hz can be 0.0 if unknown.

mcdsl_return_et mcdsdl_set_mcdsconfig_with_file(const mcd_char_t *mcdsc_file_name);
// Returns SUCCESS || COULD_NOT_OPEN_MCDSC_FILE || ERROR_READING_MCDSC_FILE
// The .mcdsc/.mcds file provides the device configuration including MCDS

mcdsl_return_et mcdsdl_decode(const uint8_t* trace_data, uint32_t trace_size_kb,
							  mctrl_trace_data_format_et tdf, mcdsdl_decoding_mode_et dm);
// Returns SUCCESS || DECODING_ERRORS
// Decode trace data, maximum value of trace_size_kb is MCDSDL_MAX_TRACE_DATA_SIZE_KB
// If called mcdsdl_decode(NULL, 0, MCTRL_TDF_KEEP, dm) the already available trace data is
// decoded with the (new) decoding mode dm. This allows to change for a trace file from 
// MCDSDL_DM_SINGLE to MCDSDL_DM_RAW.

void mcdsdl_clear();  // Clear trace data

const mcdsdl_error_counter_st* mcdsdl_get_error_counter();
// Get details in case of DECODING_ERRORS

const mcdsdl_trace_marker_st* mcdsdl_get_trace_marker();
// Get trace marker

typedef struct {

	const mcd_char_t *name;  // Name of tool

	uint32_t (*get_device_type) ();
	// Returns 0 if no device is connected

	mcd_bool_t (*check_device_suspended) ();
	// Returns TRUE if the device is suspended

	mcd_bool_t (*check_porst_occurred) ();
	// Returns TRUE if there was a PORST since the last call of this function

	mcd_bool_t (*read_block) (uint32_t addr, void *data, uint32_t num_bytes, uint32_t *num_bytes_ok);
	// Returns TRUE on success

} mcdsdl_device_callback_st;

void mcdsdl_set_device_callback(mctrl_rcfd_et rcfd, const mcdsdl_device_callback_st *device_callback);
// Optional provide functions which can be used by the decoder to get code from the device

#ifdef __cplusplus
}
#endif

#endif // #define __mcds_lib_decoder_h_


