/* tas_client.h - minimal TAS wire-protocol client (prototype).
 *
 * The TAS wire protocol was decoded from live captures (see
 * tas_server_analysis docs).  Message framing:
 *   PL2 : uint32 LE total_size (includes itself) followed by payload
 *   PL1 : payload = sequence of elements
 *         element = [wl byte][cmd byte][params...], size = (wl+1)*4 bytes
 *   Control messages use a single element covering the whole payload.
 *   PL0 transaction frames use START/items/END elements (handled later).
 *
 * Request/response encodings implemented here were verified against raw
 * capture bytes (SERVER_CONNECT / GET_TARGETS / PING).
 */
#ifndef MCD_TAS_TAS_CLIENT_H
#define MCD_TAS_TAS_CLIENT_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdint.h>
#include <stddef.h>

#define TAS_DEFAULT_PORT   24817
#define TAS_MAX_TARGETS    32
#define TAS_ERR_OK         0x08u   /* TAS PL1 success code                */
#define TAS_PL0_OK         0x80u   /* TAS PL0 success code                */

/* PL1 client types (tas_client_type_et) */
#define TAS_CLIENT_TYPE_RW     1u

/* device unlock options (tas_dev_unlock_option_et) */
#define TAS_ULO_HOT_ATTACH     0x0001u

/* PL0 address maps (tas_am15_am14.h); AM15 is server internal, 0 is
 * the default device access map. */
#define TAS_AM0                0u
#define TAS_AM15               15u

/* TAS PL1 error codes of interest (tas_pl_err_et) */
#define TAS_PL_ERR_PARAM          0x10u
#define TAS_PL_ERR_NOT_SUPPORTED  0x12u
#define TAS_PL_ERR_USAGE          0x14u
#define TAS_PL_ERR_PROTOCOL       0x1Eu
#define TAS_PL1_ERR_CMD_FAILED    0x20u
#define TAS_PL1_ERR_SERVER_LOCKED 0x32u
#define TAS_PL1_ERR_SESSION       0x38u
#define TAS_PL1_ERR_DEV_RESET     0x44u
#define TAS_PL1_ERR_DEV_LOCKED    0x46u
#define TAS_PL1_ERR_DEV_ACCESS    0x48u

/* PL0 error codes */
#define TAS_PL0_ERR_ACC_MODE      0x89u
#define TAS_PL0_ERR_ADDR_MAP      0x8Au
#define TAS_PL0_ERR_ADDR_BLOCKED  0x8Bu
#define TAS_PL0_ERR_DATA          0x8Du

typedef struct tas_conn {
    SOCKET  sock;
    int     connected;
    uint16_t pl1_cnt;          /* PL0 frame counter, starts at 0xFFC0 */
} tas_conn;

typedef struct tas_server_info {
    char     server[64];    /* server name from SERVER_CONNECT response  */
    uint16_t ver_a;
    uint16_t ver_b;
    uint32_t protocols;     /* protocol feature bitfield                 */
    uint32_t channels;      /* communication channel feature bitfield    */
    uint32_t trace_feat;    /* trace feature bitfield                    */
    char     date[16];      /* build date string "Apr 30 2026"           */
} tas_server_info;

typedef struct tas_target {
    char     identifier[65];   /* e.g. "DAS JTAG AURDUINO 46YPEADW"     */
    uint32_t device_type;      /* IEEE 1149.1 idcode                    */
    uint32_t device_id[4];     /* 16 byte device id / hash words        */
    uint8_t  phys;             /* physical configuration hint           */
    uint8_t  nclient;          /* clients already attached              */
} tas_target;

/* Returns 0 on success, -1 on transport error, -2 on protocol mismatch.
 * For protocol functions *tas_err receives the TAS error byte; TAS_ERR_OK
 * means the server accepted the request. */
int  tas_connect(tas_conn *c, const char *host, uint16_t port,
                 char *err, size_t errlen);
void tas_disconnect(tas_conn *c);

int  tas_server_connect(tas_conn *c, const char *name, const char *user,
                        uint32_t pid, tas_server_info *info, uint8_t *tas_err);
int  tas_get_targets(tas_conn *c, tas_target *targets, int max_targets,
                     int *count, uint8_t *tas_err);
int  tas_ping(tas_conn *c, uint8_t *tas_err);

/* ------------------------------------------------------------------ */
/* M2: session info and PL0 read/write                                 */
/* ------------------------------------------------------------------ */

/* tas_con_info_st as sent by the TasServer (124 bytes). */
typedef struct tas_con_info {
    uint32_t max_pl2rq_pkt_size;
    uint32_t max_pl2rsp_pkt_size;
    uint32_t device_type;      /* IEEE 1149.1 idcode, 0 if unknown      */
    uint32_t device_id[4];     /* 128 bit unique device id              */
    uint32_t ipv4_addr;
    uint16_t dev_con_feat;
    uint8_t  dev_con_phys;
    uint8_t  pl0_max_num_rw;   /* max PL0 transactions per PL1 frame    */
    uint16_t pl0_rw_mode_mask;
    uint16_t pl0_addr_map_mask;
    uint16_t msg_length_c2d;
    uint16_t msg_length_d2c;
    uint8_t  msg_num_c2d;
    uint8_t  msg_num_d2c;
    char     identifier[65];   /* access HW name                        */
    uint32_t device_id_hash;
    char     hash_str[9];      /* e.g. "LBHJYL"                         */
} tas_con_info;

/* One PL0 read/write transaction.
 *  - num_bytes 1/2/4/8 use RD8..RD64 / WR8..WR64.
 *  - reads with num_bytes in (8,1024], multiple of 8, 8-byte aligned
 *    use RDBLK; writes > 8 use WRBLK (multiple of 4, <= 1024).       */
typedef struct tas_rw_item {
    uint8_t   addr_map;        /* TAS_AM0 by default                    */
    uint64_t  addr;            /* absolute address                      */
    uint32_t  num_bytes;
    int       is_write;
    uint8_t  *data;            /* in for writes, out for reads          */
    int       ok;              /* out: 1 on success                     */
    uint32_t  num_bytes_ok;    /* out                                   */
    uint8_t   pl_err;          /* out: PL0 error byte (0x80 == ok)      */
} tas_rw_item;

/* SESSION_START request (0xA1).  Response provides session info and
 * assigns the con_id used by all later messages on this connection. */
int tas_session_start(tas_conn *c, const char *identifier,
                      tas_con_info *info, uint8_t *tas_err);

/* PING (0xA2); the response carries the same information as the
 * SESSION_START response (session snapshot refresh). */
int tas_ping_con(tas_conn *c, tas_con_info *info, uint8_t *tas_err);

/* SET_DEVICE_KEY (0xAD); declares the unlock option and key used by
 * the following DEVICE_CONNECT.  key == NULL with key_len == 0 is
 * valid (observed client behavior for hot attach). */
int tas_set_device_key(tas_conn *c, uint16_t unlock_option,
                       const uint8_t *key, uint16_t key_len,
                       uint8_t *tas_err);

/* DEVICE_CONNECT (0xA8); option: 0 = hot attach, or reset options
 * (tas_dev_con_feat_et RESET/RESET_AND_HALT/UNLOCK). */
int tas_device_connect(tas_conn *c, uint16_t option,
                       uint16_t *feat_used, uint32_t *device_type,
                       uint8_t *tas_err);

/* Execute a list of PL0 read/write transactions against the session.
 * Frames are built exactly like the reference TasClient:
 *   [PL2 len][START(pl1_cnt, addr_map_mask)][ctx items][items][END(n)]
 * One frame holds at most max_num_rw (0 -> 32) transactions and is
 * limited by the negotiated PL2 sizes from *info* if enabled.
 * Per-item results are written back into items[].  Returns 0 when all
 * frames were processed (check items[].ok), -1 transport error,
 * -2 protocol mismatch, -3 PL0 frame rejected by server (the PL1
 * error byte is stored in *pl0_err), -4 invalid item description. */
int tas_rw_exec(tas_conn *c, tas_rw_item *items, uint32_t n_items,
                uint32_t max_num_rw, uint8_t *pl0_err);

#endif /* MCD_TAS_TAS_CLIENT_H */
