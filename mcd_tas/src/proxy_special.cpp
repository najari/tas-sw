/* proxy_special.cpp - hand-written forwarders with detailed logging
 * for the four functions that matter for the error-display calibration. */
#include "mcd_api.h"
#include "proxy_common.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* mcd_open_core_f                                                     */
/* ------------------------------------------------------------------ */
typedef mcd_return_et (*fn_open_core_t)(const mcd_core_con_info_st *,
                                        mcd_core_st **);

extern "C" MCD_SHARED_LIBRARY_API
mcd_return_et mcd_open_core_f(const mcd_core_con_info_st *core_con_info,
                              mcd_core_st **core)
{
    static fn_open_core_t pf = 0;
    if (!pf)
        pf = (fn_open_core_t)proxy_orig_proc("mcd_open_core_f");

    if (core_con_info) {
        proxy_log("+ open_core: host='%s' port=%u acc_hw='%s' system='%s' "
                  "device='%s' core='%s' devtype=0x%08X core_type=%u",
                  core_con_info->host, core_con_info->server_port,
                  core_con_info->acc_hw, core_con_info->system,
                  core_con_info->device, core_con_info->core,
                  core_con_info->device_type, core_con_info->core_type);
    } else {
        proxy_log("+ open_core: (null con_info)");
    }

    mcd_return_et r = pf(core_con_info, core);
    proxy_log("- open_core -> %d (core=%p)", (int)r,
              core ? (void *)*core : NULL);
    return r;
}

/* ------------------------------------------------------------------ */
/* mcd_execute_txlist_f                                                */
/* ------------------------------------------------------------------ */
typedef mcd_return_et (*fn_exec_txlist_t)(const mcd_core_st *,
                                          mcd_txlist_st *);

extern "C" MCD_SHARED_LIBRARY_API
mcd_return_et mcd_execute_txlist_f(const mcd_core_st *core,
                                   mcd_txlist_st *txlist)
{
    static fn_exec_txlist_t pf = 0;
    if (!pf)
        pf = (fn_exec_txlist_t)proxy_orig_proc("mcd_execute_txlist_f");

    uint32_t i;
    proxy_log("+ execute_txlist: core=%p txlist=%p", (const void *)core,
              (void *)txlist);
    if (txlist) {
        proxy_log("    num_tx=%u num_tx_ok(pre)=%u", txlist->num_tx,
                  txlist->num_tx_ok);
        for (i = 0; i < txlist->num_tx && i < 8; i++) {
            const mcd_tx_st *t = &txlist->tx[i];
            proxy_log("    tx[%u] PRE: at=%d opts=0x%X space=%u addr=0x%llX "
                      "bytes=%u width=%u mode=%u data=%p",
                      i, (int)t->access_type, (unsigned)t->options,
                      (unsigned)t->addr.mem_space_id,
                      (unsigned long long)t->addr.address,
                      (unsigned)t->num_bytes, (unsigned)t->access_width,
                      (unsigned)t->core_mode, (void *)t->data);
        }
    }

    mcd_return_et r = pf(core, txlist);
    proxy_log("- execute_txlist -> %d", (int)r);

    if (txlist) {
        proxy_log("    num_tx_ok(post)=%u", txlist->num_tx_ok);
        for (i = 0; i < txlist->num_tx && i < 8; i++) {
            const mcd_tx_st *t = &txlist->tx[i];
            char hex[3 * 16 + 1];
            hex[0] = 0;
            if (t->data && t->num_bytes) {
                uint32_t n = t->num_bytes > 16 ? 16 : t->num_bytes, k;
                char *w = hex;
                for (k = 0; k < n; k++)
                    w += sprintf(w, "%02X ", t->data[k]);
            }
            proxy_log("    tx[%u] POST: num_bytes_ok=%u data=[%s]", i,
                      (unsigned)t->num_bytes_ok, hex);
        }
    }
    return r;
}

/* ------------------------------------------------------------------ */
/* mcd_qry_error_info_f                                                */
/* ------------------------------------------------------------------ */
typedef void (*fn_qry_err_t)(const mcd_core_st *, mcd_error_info_st *);

extern "C" MCD_SHARED_LIBRARY_API
void mcd_qry_error_info_f(const mcd_core_st *core,
                          mcd_error_info_st *error_info)
{
    static fn_qry_err_t pf = 0;
    if (!pf)
        pf = (fn_qry_err_t)proxy_orig_proc("mcd_qry_error_info_f");

    pf(core, error_info);

    if (error_info) {
        proxy_log("- qry_error_info: return_status=%d code=0x%X events=0x%X "
                  "str='%s'",
                  (int)error_info->return_status,
                  (unsigned)error_info->error_code,
                  (unsigned)error_info->error_events, error_info->error_str);
    } else {
        proxy_log("- qry_error_info: (null)");
    }
}

/* ------------------------------------------------------------------ */
/* mcd_qry_state_f                                                     */
/* ------------------------------------------------------------------ */
typedef mcd_return_et (*fn_qry_state_t)(const mcd_core_st *,
                                        mcd_core_state_st *);

extern "C" MCD_SHARED_LIBRARY_API
mcd_return_et mcd_qry_state_f(const mcd_core_st *core, mcd_core_state_st *state)
{
    static fn_qry_state_t pf = 0;
    if (!pf)
        pf = (fn_qry_state_t)proxy_orig_proc("mcd_qry_state_f");

    mcd_return_et r = pf(core, state);
    if (r == 0 && state) {
        proxy_log("- qry_state -> 0: state=%d event=0x%X hw_thread=%u "
                  "trig=%u stop='%s' info='%s'",
                  (int)state->state, (unsigned)state->event,
                  state->hw_thread_id, state->trig_id, state->stop_str,
                  state->info_str);
    } else {
        proxy_log("- qry_state -> %d", (int)r);
    }
    return r;
}

/* ------------------------------------------------------------------ */
/* mcd_qry_reg_map_f                                                   */
/* ------------------------------------------------------------------ */
typedef mcd_return_et (*fn_qry_reg_map_t)(const mcd_core_st *, uint32_t,
                                          uint32_t, uint32_t *,
                                          mcd_register_info_st *);
typedef mcd_return_et (*fn_qry_reg_groups_t)(const mcd_core_st *, uint32_t,
                                             uint32_t *,
                                             mcd_register_group_st *);

static void dump_reg_entry(uint32_t i, const mcd_register_info_st *ri)
{
    proxy_log("    reg[%u]: name='%s' group=%u space=%u addr=0x%llX "
              "size=%u type=%d hw_thread=%u rmask=0x%X wmask=0x%X "
              "side_r=%d side_w=%d",
              i, ri->regname, (unsigned)ri->reg_group_id,
              (unsigned)ri->addr.mem_space_id,
              (unsigned long long)ri->addr.address,
              (unsigned)ri->regsize, (int)ri->reg_type,
              (unsigned)ri->hw_thread_id,
              (unsigned)ri->core_mode_mask_read,
              (unsigned)ri->core_mode_mask_write,
              (int)ri->has_side_effects_read,
              (int)ri->has_side_effects_write);
}

extern "C" MCD_SHARED_LIBRARY_API
mcd_return_et mcd_qry_reg_map_f(const mcd_core_st *core, uint32_t reg_group_id,
                                uint32_t start_index, uint32_t *num_regs,
                                mcd_register_info_st *reg_info)
{
    static fn_qry_reg_map_t pf = 0;
    static int full_dumped = 0;
    uint32_t cap_in = num_regs ? *num_regs : 0;

    if (!pf)
        pf = (fn_qry_reg_map_t)proxy_orig_proc("mcd_qry_reg_map_f");

    proxy_log("+ qry_reg_map: group=%u start=%u cap=%u",
              (unsigned)reg_group_id, (unsigned)start_index,
              (unsigned)cap_in);

    mcd_return_et r = pf(core, reg_group_id, start_index, num_regs, reg_info);
    proxy_log("- qry_reg_map -> %d num=%u", (int)r,
              num_regs ? (unsigned)*num_regs : 0u);

    if (cap_in > 0 && r == 0 && reg_info && num_regs && *num_regs > 0) {
        uint32_t i, n = *num_regs > 16 ? 16 : *num_regs;
        for (i = 0; i < n; i++)
            dump_reg_entry(i, &reg_info[i]);
    }

    /* One-shot calibration probe: on the first count query, fetch the
     * complete register map (per index) and the register group list. */
    if (!full_dumped && cap_in == 0 && r == 0 && num_regs && *num_regs > 0) {
        uint32_t total = *num_regs, i;
        full_dumped = 1;
        if (total > 64)
            total = 64;
        proxy_log("[FULL DUMP] registering %u entries (group %u)",
                  total, (unsigned)reg_group_id);
        for (i = 0; i < total; i++) {
            mcd_register_info_st ri;
            uint32_t cap = 1;
            memset(&ri, 0, sizeof(ri));
            mcd_return_et r2 = pf(core, reg_group_id, i, &cap, &ri);
            if (r2 == 0 && cap > 0)
                dump_reg_entry(i, &ri);
            else
                proxy_log("    reg[%u]: fetch rc=%d num=%u", i, (int)r2,
                          (unsigned)cap);
        }
        /* register group list */
        {
            fn_qry_reg_groups_t gpf =
                (fn_qry_reg_groups_t)proxy_orig_proc("mcd_qry_reg_groups_f");
            if (gpf) {
                uint32_t ng = 0;
                mcd_return_et r3 = gpf(core, 0, &ng, NULL);
                proxy_log("[FULL DUMP] qry_reg_groups cap=0 -> %d num=%u",
                          (int)r3, (unsigned)ng);
                if (r3 == 0 && ng > 0 && ng <= 16) {
                    for (i = 0; i < ng; i++) {
                        mcd_register_group_st g;
                        uint32_t cap = 1;
                        memset(&g, 0, sizeof(g));
                        mcd_return_et r4 = gpf(core, i, &cap, &g);
                        if (r4 == 0 && cap > 0)
                            proxy_log("    grp[%u]: id=%u name='%s' n=%u",
                                      i, (unsigned)g.reg_group_id,
                                      g.reg_group_name,
                                      (unsigned)g.n_registers);
                        else
                            proxy_log("    grp[%u]: fetch rc=%d num=%u", i,
                                      (int)r4, (unsigned)cap);
                    }
                }
            }
        }
    }
    return r;
}

/* ------------------------------------------------------------------ */
/* mcd_qry_trig_set_state_f                                            */
/* ------------------------------------------------------------------ */
typedef mcd_return_et (*fn_qry_trig_set_state_t)(const mcd_core_st *,
                                                 mcd_trig_set_state_st *);

extern "C" MCD_SHARED_LIBRARY_API
mcd_return_et mcd_qry_trig_set_state_f(const mcd_core_st *core,
                                       mcd_trig_set_state_st *trig_state)
{
    static fn_qry_trig_set_state_t pf = 0;
    if (!pf)
        pf = (fn_qry_trig_set_state_t)proxy_orig_proc("mcd_qry_trig_set_state_f");

    mcd_return_et r = pf(core, trig_state);
    if (r == 0 && trig_state) {
        proxy_log("- qry_trig_set_state -> 0: active=%d state=%u state_valid=%d "
                  "trig_bus=0x%X bus_valid=%d trace=%d trace_valid=%d "
                  "analysis=%d analysis_valid=%d",
                  (int)trig_state->active, (unsigned)trig_state->state,
                  (int)trig_state->state_valid, (unsigned)trig_state->trig_bus,
                  (int)trig_state->trig_bus_valid, (int)trig_state->trace,
                  (int)trig_state->trace_valid, (int)trig_state->analysis,
                  (int)trig_state->analysis_valid);
    } else {
        proxy_log("- qry_trig_set_state -> %d", (int)r);
    }
    return r;
}
