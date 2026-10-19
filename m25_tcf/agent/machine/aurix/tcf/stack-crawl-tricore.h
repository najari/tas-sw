/*
 * stack-crawl-tricore.h - TriCore CSA (Context Save Area) stack crawl.
 *
 * SPDX-License-Identifier: MIT (project code only).
 */
#ifndef D_stack_crawl_tricore
#define D_stack_crawl_tricore

#include <tcf/config.h>
#include <tcf/framework/cpudefs.h>

/*
 * Walk one frame up the CSA chain: fill 'down' (the caller frame) from the
 * CSA pointed to by the current frame's PCXI.
 * Returns 0 on success (also when the end of the chain is reached - 'down'
 * is left without register data in that case), -1 on error.
 */
extern int crawl_stack_frame_tricore(StackFrame * frame, StackFrame * down);

#endif /* D_stack_crawl_tricore */
