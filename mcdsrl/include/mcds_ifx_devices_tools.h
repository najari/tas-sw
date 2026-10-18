/*****************************************************************************
*
* Copyright (C) 2012-2024 Infineon Technologies AG. All rights reserved.
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
* MODULE:  mcds_ifx_devices_tools.h
******************************************************************************
* DESCRIPTION:
* Tools and information about IFX devices
******************************************************************************/

#pragma once

#include "mcd_types.h"

#include <assert.h>

//-------------------------------------------------------------------------------------------------
#define MIDT_DEVICE_TYPE_VERSION_MASK_OUT  0x0FFFFFFF  // JTAG ID version nibble masked out

//-------------------------------------------------------------------------------------------------
typedef enum midt_device_type_t {

	// AURIX
	MIDT_DEVICE_TYPE_TC21X = 0x00202083,
	MIDT_DEVICE_TYPE_TC21X_A = 0x10202083,

	MIDT_DEVICE_TYPE_TC22X = 0x00201083,
	MIDT_DEVICE_TYPE_TC22X_A = 0x10201083,

	MIDT_DEVICE_TYPE_TC23X = 0x00200083,
	MIDT_DEVICE_TYPE_TC23X_A = 0x10200083,

	MIDT_DEVICE_TYPE_TC26X = 0x001E8083,
	MIDT_DEVICE_TYPE_TC26X_A = 0x101E8083,
	MIDT_DEVICE_TYPE_TC26X_B = 0x201E8083,

	MIDT_DEVICE_TYPE_TC27X = 0x001DA083,
	MIDT_DEVICE_TYPE_TC2D5ED = 0x101DA083,
	MIDT_DEVICE_TYPE_TC27X_A = 0x201DA083,
	MIDT_DEVICE_TYPE_TC27X_B = 0x301DA083,
	MIDT_DEVICE_TYPE_TC27X_C = 0x401DA083,
	MIDT_DEVICE_TYPE_TC27X_D = 0x501DA083,

	MIDT_DEVICE_TYPE_TC29X = 0x001E9083,
	MIDT_DEVICE_TYPE_TC29X_A = 0x101E9083,
	MIDT_DEVICE_TYPE_TC29X_B = 0x201E9083,

	// A2G
	MIDT_DEVICE_TYPE_TC33X   = 0x0020B083,
	MIDT_DEVICE_TYPE_TC33X_A = 0x1020B083,

	MIDT_DEVICE_TYPE_TC33XE   = 0x0020C083,
	MIDT_DEVICE_TYPE_TC33XE_A = 0x1020C083,

	MIDT_DEVICE_TYPE_TC35X   = 0x0020A083,
	MIDT_DEVICE_TYPE_TC35X_A = 0x1020A083,

	MIDT_DEVICE_TYPE_TC36X   = 0x00209083,
	MIDT_DEVICE_TYPE_TC36X_A = 0x10209083,

	MIDT_DEVICE_TYPE_TC37X   = 0x00207083,
	MIDT_DEVICE_TYPE_TC37X_A = 0x10207083,

	MIDT_DEVICE_TYPE_TC37XE   = 0x00208083,
	MIDT_DEVICE_TYPE_TC37XE_A = 0x10208083,

	MIDT_DEVICE_TYPE_TC38X   = 0x00206083,
	MIDT_DEVICE_TYPE_TC38X_A = 0x10206083,

	MIDT_DEVICE_TYPE_TC3EX   = 0x00215083,
	MIDT_DEVICE_TYPE_TC3EX_A = 0x10215083,

	MIDT_DEVICE_TYPE_TC39X   = 0x00205083,
	MIDT_DEVICE_TYPE_TC39X_A = 0x10205083,
	MIDT_DEVICE_TYPE_TC39X_B = 0x20205083,

	MIDT_DEVICE_TYPE_DAPEA2G = 0x0000DA9E,  // A2G DAPE connection (value was defined by DAS_DID0_IFX_JTAG_DAPE before)

	MIDT_DEVICE_TYPE_TC41X   = 0x00218083,
	MIDT_DEVICE_TYPE_TC41X_A = 0x10218083,

	MIDT_DEVICE_TYPE_TC42X   = 0x00219083,
	MIDT_DEVICE_TYPE_TC42X_A = 0x10219083,

	MIDT_DEVICE_TYPE_TC44X   = 0x0021A083,
	MIDT_DEVICE_TYPE_TC44X_A = 0x1021A083,

	MIDT_DEVICE_TYPE_TC45X   = 0x0021B083,
	MIDT_DEVICE_TYPE_TC45X_A = 0x1021B083,

	MIDT_DEVICE_TYPE_TC46X   = 0x0021C083,
	MIDT_DEVICE_TYPE_TC46X_A = 0x1021C083,

	MIDT_DEVICE_TYPE_TC48X   = 0x0021D083,
	MIDT_DEVICE_TYPE_TC48X_A = 0x1021D083,

	MIDT_DEVICE_TYPE_TC49AA  = 0x0021E083,  // TC49A
	MIDT_DEVICE_TYPE_TC49AAA = 0x1021E083,

	MIDT_DEVICE_TYPE_TC49X   = 0x0022B083,  // TC49N
	MIDT_DEVICE_TYPE_TC49X_A = 0x1022B083,

	MIDT_DEVICE_TYPE_TC4DX   = 0x00225083,
	MIDT_DEVICE_TYPE_TC4DX_A = 0x10225083,

	MIDT_DEVICE_TYPE_TC4EX   = 0x00231083,
	MIDT_DEVICE_TYPE_TC4EX_A = 0x10231083,

	MIDT_DEVICE_TYPE_TC4PX   = 0x0022F083,
	MIDT_DEVICE_TYPE_TC4PX_A = 0x1022F083,

	MIDT_DEVICE_TYPE_TC4ZX   = 0x00230083,
	MIDT_DEVICE_TYPE_TC4ZX_A = 0x10230083,

} midt_device_type_t;

inline bool midt_is_tc4d(uint32_t device_type)  { return ((device_type & MIDT_DEVICE_TYPE_VERSION_MASK_OUT) == MIDT_DEVICE_TYPE_TC4DX); }
inline bool midt_is_tc49a(uint32_t device_type) { return ((device_type & MIDT_DEVICE_TYPE_VERSION_MASK_OUT) == MIDT_DEVICE_TYPE_TC49AA); }

//-------------------------------------------------------------------------------------------------
inline bool midt_check_if_a1g(uint32_t device_type)
{
	switch (device_type & MIDT_DEVICE_TYPE_VERSION_MASK_OUT) {
	case MIDT_DEVICE_TYPE_TC21X:
	case MIDT_DEVICE_TYPE_TC22X:
	case MIDT_DEVICE_TYPE_TC23X:
	case MIDT_DEVICE_TYPE_TC26X:
	case MIDT_DEVICE_TYPE_TC27X:
	case MIDT_DEVICE_TYPE_TC29X:
		return true;
	default:
		return false;
	}
}

//-------------------------------------------------------------------------------------------------
inline bool midt_check_if_a2g(uint32_t device_type)
{
	switch (device_type & MIDT_DEVICE_TYPE_VERSION_MASK_OUT) {
	case MIDT_DEVICE_TYPE_TC33X:
	case MIDT_DEVICE_TYPE_TC33XE:
	case MIDT_DEVICE_TYPE_TC35X:
	case MIDT_DEVICE_TYPE_TC36X:
	case MIDT_DEVICE_TYPE_TC37X:
	case MIDT_DEVICE_TYPE_TC37XE:
	case MIDT_DEVICE_TYPE_TC38X:
	case MIDT_DEVICE_TYPE_TC3EX:
	case MIDT_DEVICE_TYPE_TC39X:
	case MIDT_DEVICE_TYPE_DAPEA2G:
		return true;
	default:
		return false;
	}
}

//-------------------------------------------------------------------------------------------------
inline bool midt_check_if_a3g(uint32_t device_type)
{
	switch (device_type & MIDT_DEVICE_TYPE_VERSION_MASK_OUT) {
	case MIDT_DEVICE_TYPE_TC42X:
	case MIDT_DEVICE_TYPE_TC44X:
	case MIDT_DEVICE_TYPE_TC45X:
	case MIDT_DEVICE_TYPE_TC46X:
	case MIDT_DEVICE_TYPE_TC48X:
	case MIDT_DEVICE_TYPE_TC49AA:
	case MIDT_DEVICE_TYPE_TC49X:
	case MIDT_DEVICE_TYPE_TC4DX:
	case MIDT_DEVICE_TYPE_TC4EX:
	case MIDT_DEVICE_TYPE_TC4PX:
	case MIDT_DEVICE_TYPE_TC4ZX:
		return true;
	default:
		return false;
	}
}

//-------------------------------------------------------------------------------------------------
typedef enum midt_aurix_gen_t {
	MIDT_AG_UNKNOWN = 0,
	MIDT_A1G        = 1,
	MIDT_A2G        = 2,
	MIDT_A3G        = 3,
} midt_aurix_gen_t;

//-------------------------------------------------------------------------------------------------
inline midt_aurix_gen_t midt_check_aurix_generation(uint32_t device_type)
{
	if (midt_check_if_a3g(device_type))
		return midt_aurix_gen_t::MIDT_A3G;
	if (midt_check_if_a2g(device_type))
		return midt_aurix_gen_t::MIDT_A2G;
	if (midt_check_if_a1g(device_type))
		return midt_aurix_gen_t::MIDT_A1G;
	return midt_aurix_gen_t::MIDT_AG_UNKNOWN;
}

//-------------------------------------------------------------------------------------------------
inline uint8_t midt_get_num_cpus(uint32_t device_type)
{
	switch (device_type & MIDT_DEVICE_TYPE_VERSION_MASK_OUT) {
	case MIDT_DEVICE_TYPE_TC21X:
	case MIDT_DEVICE_TYPE_TC22X:
	case MIDT_DEVICE_TYPE_TC23X:
	case MIDT_DEVICE_TYPE_TC33X:
		return 1;
	case MIDT_DEVICE_TYPE_TC26X:
	case MIDT_DEVICE_TYPE_TC33XE:
	case MIDT_DEVICE_TYPE_TC36X:
		return 2;
	case MIDT_DEVICE_TYPE_TC27X:
	case MIDT_DEVICE_TYPE_TC29X:
	case MIDT_DEVICE_TYPE_TC35X:
	case MIDT_DEVICE_TYPE_TC37X:
	case MIDT_DEVICE_TYPE_TC37XE:
		return 3;
	case MIDT_DEVICE_TYPE_TC38X:
	case MIDT_DEVICE_TYPE_TC3EX:
		return 4;
	case MIDT_DEVICE_TYPE_TC39X:
		return 6;
	default: assert(false);
		return 0;
	}
}

//-------------------------------------------------------------------------------------------------
inline uint32_t midt_get_emem_tcm_size_kb(uint32_t device_type)
{
	if (device_type == MIDT_DEVICE_TYPE_TC39X_A)
		return 1024;

	switch (device_type & MIDT_DEVICE_TYPE_VERSION_MASK_OUT) {

	case MIDT_DEVICE_TYPE_TC23X:
	case MIDT_DEVICE_TYPE_TC26X:    return  512;
	case MIDT_DEVICE_TYPE_TC27X:
	case MIDT_DEVICE_TYPE_TC29X:    return 1024;

	case MIDT_DEVICE_TYPE_TC33XE:  return 1024;
	case MIDT_DEVICE_TYPE_TC35X:
	case MIDT_DEVICE_TYPE_TC37XE:
	case MIDT_DEVICE_TYPE_TC39X:    return 2048;  // A-step with 1024 handled above

	default: assert(false);         return 0;
	}
}

//-------------------------------------------------------------------------------------------------
inline uint32_t midt_get_emem_tcm_base_addr(uint32_t device_type)
{
	if (midt_check_if_a1g(device_type)) {
		return 0xAF000000;
	}
	else {
		assert(midt_check_if_a2g(device_type));
		return 0xB9000000;
	}
}

//-------------------------------------------------------------------------------------------------
inline uint32_t midt_get_emem_xtm_base_addr(uint32_t device_type)
{
	if (midt_check_if_a1g(device_type)) {
		switch (device_type & MIDT_DEVICE_TYPE_VERSION_MASK_OUT) {
		case MIDT_DEVICE_TYPE_TC23X:
		case MIDT_DEVICE_TYPE_TC26X:    return 0xAF100000;
		case MIDT_DEVICE_TYPE_TC27X:    return 0;
		case MIDT_DEVICE_TYPE_TC29X:    return 0xAF200000;
		default: assert(false);         return 0;
		}
	}
	else {
		assert(midt_check_if_a2g(device_type));
		return 0xB9400000;
	}
}

//-------------------------------------------------------------------------------------------------
inline uint32_t midt_get_emem_tcm_tile_size_kb(uint32_t device_type)
{
	if (midt_check_if_a1g(device_type))
		return 64;

	assert(midt_check_if_a2g(device_type));
	return 256;
}

//-------------------------------------------------------------------------------------------------
inline uint32_t midt_get_global_addr_from_segment_c_d(midt_aurix_gen_t aurix_gen, uint8_t i_cpu, uint32_t scd_addr)
{
	assert((scd_addr & 0xE0000000) == 0xC0000000);  // Segment C or D address

	uint32_t addr = 0;  // Default in case of errors

	if (aurix_gen == MIDT_A3G) {
		assert(i_cpu < 7);  // Index 6 is CPUcs
		if ((scd_addr >= 0xC0000000) && (scd_addr < 0xC0400000)) {  // 4MB range
			addr = ((scd_addr & 0x3FFFFF) | 0x70100000) - i_cpu * 0x10000000;
		}
		else if ((scd_addr >= 0xD0000000) && (scd_addr < 0xD0400000)) {
			addr = ((scd_addr & 0x3FFFFF) | 0x70000000) - i_cpu * 0x10000000;
		}
	}
	else if (aurix_gen == MIDT_A2G) {
		assert(i_cpu < 6);
		if ((scd_addr >= 0xC0000000) && (scd_addr < 0xC0400000)) {  // 4MB range
			if (i_cpu == 5)
				addr = (scd_addr & 0x3FFFFF) | 0x10100000;
			else
				addr = ((scd_addr & 0x3FFFFF) | 0x70100000) - i_cpu * 0x10000000;
		}
		else if ((scd_addr >= 0xD0000000) && (scd_addr < 0xD0400000)) {
			if (i_cpu == 5)
				addr = (scd_addr & 0x3FFFFF) | 0x10000000;
			else
				addr = ((scd_addr & 0x3FFFFF) | 0x70000000) - i_cpu * 0x10000000;
		}
	}
	else if (aurix_gen == MIDT_A1G) {
		assert(i_cpu < 3);
		if ((scd_addr >= 0xC0000000) && (scd_addr < 0xC0400000)) {  // 4MB range
			addr = ((scd_addr & 0x3FFFFF) | 0x70100000) - i_cpu * 0x10000000;
		}
		else if ((scd_addr >= 0xD0000000) && (scd_addr < 0xD0400000)) {
			addr = ((scd_addr & 0x3FFFFF) | 0x70000000) - i_cpu * 0x10000000;
		}
	}

	return addr;
}

//-------------------------------------------------------------------------------------------------
inline uint32_t midt_get_srn_tos_value_for_cpus(uint32_t device_type, uint32_t cpu_index)
{
	if (midt_check_if_a1g(device_type)) {
		assert(cpu_index < 3);
		return cpu_index;
	}
	else {
		assert(midt_check_if_a2g(device_type));
		if (cpu_index == 0)
			return 0;
		return cpu_index + 1;  // DMA in between
	}
}

//-------------------------------------------------------------------------------------------------
inline void midt_get_srn_index_range_for_stms(uint32_t device_type, uint32_t *srn_index_first, uint32_t *srn_index_last)
{
	switch (device_type & MIDT_DEVICE_TYPE_VERSION_MASK_OUT) {
	case MIDT_DEVICE_TYPE_TC21X:
	case MIDT_DEVICE_TYPE_TC22X:
	case MIDT_DEVICE_TYPE_TC23X:
		*srn_index_first = 54;  // STM0SR0
		*srn_index_last  = 55;  // STM0SR1
		break;
	case MIDT_DEVICE_TYPE_TC26X:
		*srn_index_first =  98;  // STM0SR0
		*srn_index_last  = 101;  // STM1SR1
		break;
	case MIDT_DEVICE_TYPE_TC27X:
		*srn_index_first = 103;  // STM0SR0
		*srn_index_last  = 108;  // STM2SR1
		break;
	case MIDT_DEVICE_TYPE_TC29X:
		*srn_index_first = 131;  // STM0SR0
		*srn_index_last  = 136;  // STM2SR1
		break;
	// Rule for A2G: SRC register address - module base address divided by 4
	// STM0SR0 (0xF0038300 - 0xF0038000)/4 = 192
	case MIDT_DEVICE_TYPE_TC33X:
	case MIDT_DEVICE_TYPE_TC33XE:
		assert(false);
		*srn_index_first = 0;
		*srn_index_last  = 0;
		break;
	case MIDT_DEVICE_TYPE_TC36X:
		*srn_index_first = 192;  // STM0SR0
		*srn_index_last  = 195;  // STM1SR1
		break;
	case MIDT_DEVICE_TYPE_TC35X:
	case MIDT_DEVICE_TYPE_TC37X:
	case MIDT_DEVICE_TYPE_TC37XE:
		*srn_index_first = 192;  // STM0SR0
		*srn_index_last  = 197;  // STM2SR1
		break;
	case MIDT_DEVICE_TYPE_TC38X:
	case MIDT_DEVICE_TYPE_TC3EX:
		*srn_index_first = 192;  // STM0SR0
		*srn_index_last  = 199;  // STM3SR1
		break;
	case MIDT_DEVICE_TYPE_TC39X:
		*srn_index_first = 192;  // STM0SR0
		*srn_index_last  = 203;  // STM5SR1
		break;
	default: assert(false);
	}
}

//-------------------------------------------------------------------------------------------------
inline void midt_get_srn_index_range_for_gpsr(uint32_t device_type, uint32_t *srn_index_first, uint32_t *srn_index_last)
{
	switch (device_type & MIDT_DEVICE_TYPE_VERSION_MASK_OUT) {
	case MIDT_DEVICE_TYPE_TC21X:
	case MIDT_DEVICE_TYPE_TC22X:
	case MIDT_DEVICE_TYPE_TC23X:
		*srn_index_first = 146;  // GPSR00
		*srn_index_last  = 149;  // GPSR03
		break;
	case MIDT_DEVICE_TYPE_TC26X:
		*srn_index_first = 244;  // GPSR00
		*srn_index_last  = 351;  // GPSR13
		break;
	case MIDT_DEVICE_TYPE_TC27X:
		*srn_index_first = 293;  // GPSR00
		*srn_index_last  = 304;  // GPSR23
		break;
	case MIDT_DEVICE_TYPE_TC29X:
		*srn_index_first = 427;  // GPSR00
		*srn_index_last  = 438;  // GPSR23
		break;
	case MIDT_DEVICE_TYPE_TC33X:
	case MIDT_DEVICE_TYPE_TC33XE:
		assert(false);
		*srn_index_first = 0;
		*srn_index_last  = 0;
		break;
	case MIDT_DEVICE_TYPE_TC36X:
		*srn_index_first = 612;  // GPSR00
		*srn_index_last  = 627;  // GPSR17
		break;
	case MIDT_DEVICE_TYPE_TC35X:
	case MIDT_DEVICE_TYPE_TC37X:
	case MIDT_DEVICE_TYPE_TC37XE:
		*srn_index_first = 612;  // GPSR00
		*srn_index_last  = 635;  // GPSR27
		break;
	case MIDT_DEVICE_TYPE_TC38X:
	case MIDT_DEVICE_TYPE_TC3EX:
	case MIDT_DEVICE_TYPE_TC39X:
		*srn_index_first = 612;  // GPSR00
		*srn_index_last  = 659;  // GPSR57
		break;
	default: assert(false);
	}
}
