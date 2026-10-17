/* proxy_special.cpp - hand-written forwarders with detailed logging
 * for the four functions that matter for the error-display calibration. */
#include "mcd_api.h"
#include "proxy_common.h"

#include <stdio.h>

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
