/* tas_client.c - minimal TAS wire-protocol client (prototype). */
#define WIN32_LEAN_AND_MEAN
#include "aurix-tas.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_wsa_started = 0;

static int ensure_wsa(char *err, size_t errlen)
{
    WSADATA w;
    if (g_wsa_started)
        return 0;
    if (WSAStartup(MAKEWORD(2, 2), &w) != 0) {
        if (err) snprintf(err, errlen, "WSAStartup failed");
        return -1;
    }
    g_wsa_started = 1;
    return 0;
}

static uint16_t get16le(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t get32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void put32le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
    p[2] = (uint8_t)((v >> 16) & 0xff);
    p[3] = (uint8_t)((v >> 24) & 0xff);
}

static int send_all(SOCKET s, const uint8_t *b, int n)
{
    int off = 0;
    while (off < n) {
        int r = send(s, (const char *)(b + off), n - off, 0);
        if (r <= 0)
            return -1;
        off += r;
    }
    return 0;
}

static int recv_all(SOCKET s, uint8_t *b, int n)
{
    int off = 0;
    while (off < n) {
        int r = recv(s, (char *)(b + off), n - off, 0);
        if (r <= 0)
            return -1;
        off += r;
    }
    return 0;
}

int tas_connect(tas_conn *c, const char *host, uint16_t port,
                char *err, size_t errlen)
{
    struct addrinfo hints, *res = NULL;
    char portstr[16];
    SOCKET s;
    int one = 1;
    DWORD tmo = 8000;

    memset(c, 0, sizeof(*c));
    c->sock = INVALID_SOCKET;

    if (ensure_wsa(err, errlen) != 0)
        return -1;

    snprintf(portstr, sizeof(portstr), "%u", (unsigned)port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if (getaddrinfo(host, portstr, &hints, &res) != 0 || res == NULL) {
        if (err) snprintf(err, errlen, "cannot resolve host '%s'", host);
        return -1;
    }

    s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s == INVALID_SOCKET) {
        freeaddrinfo(res);
        if (err) snprintf(err, errlen, "socket() failed (%d)", WSAGetLastError());
        return -1;
    }

    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof(one));
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tmo, sizeof(tmo));

    if (connect(s, res->ai_addr, (int)res->ai_addrlen) != 0) {
        int e = WSAGetLastError();
        closesocket(s);
        freeaddrinfo(res);
        if (err) snprintf(err, errlen, "connect %s:%u failed (wsa=%d)", host, (unsigned)port, e);
        return -1;
    }
    freeaddrinfo(res);

    c->sock = s;
    c->connected = 1;
    c->pl1_cnt = 0xFFC0;   /* first PL0 frame uses 0xFFC1 */
    return 0;
}

void tas_disconnect(tas_conn *c)
{
    if (c->sock != INVALID_SOCKET)
        closesocket(c->sock);
    c->sock = INVALID_SOCKET;
    c->connected = 0;
}

static int tas_send_raw(tas_conn *c, const uint8_t *payload, int len)
{
    uint8_t hdr[4];
    put32le(hdr, (uint32_t)(len + 4));
    if (send_all(c->sock, hdr, 4) != 0)
        return -1;
    if (len > 0 && send_all(c->sock, payload, len) != 0)
        return -1;
    return 0;
}

static int tas_recv_raw(tas_conn *c, uint8_t *buf, int cap, int *out_len)
{
    uint8_t hdr[4];
    uint32_t total;
    if (recv_all(c->sock, hdr, 4) != 0)
        return -1;
    total = get32le(hdr);
    if (total < 4 || total > (uint32_t)cap)
        return -2;
    if (recv_all(c->sock, buf, (int)total - 4) != 0)
        return -1;
    *out_len = (int)total - 4;
    return 0;
}

int tas_server_connect(tas_conn *c, const char *name, const char *user,
                       uint32_t pid, tas_server_info *info, uint8_t *tas_err)
{
    uint8_t p[56];
    uint8_t r[4096];
    int rl = 0;

    memset(p, 0, sizeof(p));
    p[0] = 0x0D;                 /* wl: (56/4)-1                         */
    p[1] = 0x80;                 /* SERVER_CONNECT                       */
    strncpy((char *)p + 4, name ? name : "", 31);
    strncpy((char *)p + 36, user ? user : "", 15);
    put32le(p + 52, pid);

    if (tas_send_raw(c, p, (int)sizeof(p)) != 0)
        return -1;
    if (tas_recv_raw(c, r, (int)sizeof(r), &rl) != 0)
        return -1;
    if (rl < 4 || r[1] != 0x80)
        return -2;

    *tas_err = r[3];
    if (r[3] == TAS_ERR_OK && info != NULL && rl >= 136) {
        const uint8_t *si = r + 8;
        memset(info, 0, sizeof(*info));
        memcpy(info->server, si, 64);
        info->server[63] = 0;
        info->ver_a = get16le(si + 64);
        info->ver_b = get16le(si + 66);
        info->protocols = get32le(si + 68);
        info->channels = get32le(si + 72);
        info->trace_feat = get32le(si + 76);
        memcpy(info->date, si + 96, 16);
        info->date[15] = 0;
    }
    return 0;
}

int tas_get_targets(tas_conn *c, tas_target *targets, int max_targets,
                    int *count, uint8_t *tas_err)
{
    uint8_t p[4] = { 0x00, 0x86, 0x00, 0x00 };  /* [wl=0][GET_TARGETS][start][res] */
    uint8_t r[65536];
    int rl = 0;
    int n, avail, i;

    *count = 0;
    if (tas_send_raw(c, p, 4) != 0)
        return -1;
    if (tas_recv_raw(c, r, (int)sizeof(r), &rl) != 0)
        return -1;
    if (rl < 8 || r[1] != 0x86)
        return -2;

    *tas_err = r[3];
    if (r[3] != TAS_ERR_OK)
        return 0;

    n = r[6];
    avail = (rl - 8) / 88;
    if (n > avail)
        n = avail;
    if (n > max_targets)
        n = max_targets;

    for (i = 0; i < n; i++) {
        const uint8_t *t = r + 8 + 88 * i;
        tas_target *o = &targets[i];
        memset(o, 0, sizeof(*o));
        memcpy(o->identifier, t, 64);
        o->identifier[64] = 0;
        o->device_type = get32le(t + 64);
        o->device_id[0] = get32le(t + 68);
        o->device_id[1] = get32le(t + 72);
        o->device_id[2] = get32le(t + 76);
        o->device_id[3] = get32le(t + 80);
        o->phys = t[84];
        o->nclient = t[85];
    }
    *count = n;
    return 0;
}

int tas_ping(tas_conn *c, uint8_t *tas_err)
{
    return tas_ping_con(c, NULL, tas_err);
}

/* ------------------------------------------------------------------ */
/* M2: session info and PL0 read/write                                 */
/* ------------------------------------------------------------------ */

static void put16le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
}

/* Parse the 124 byte tas_con_info_st. */
static void parse_con_info(const uint8_t *ci, tas_con_info *o)
{
    int i;
    memset(o, 0, sizeof(*o));
    o->max_pl2rq_pkt_size  = get32le(ci + 0);
    o->max_pl2rsp_pkt_size = get32le(ci + 4);
    o->device_type         = get32le(ci + 8);
    for (i = 0; i < 4; i++)
        o->device_id[i]    = get32le(ci + 12 + 4 * i);
    o->ipv4_addr           = get32le(ci + 28);
    o->dev_con_feat        = get16le(ci + 32);
    o->dev_con_phys        = ci[34];
    o->pl0_max_num_rw      = ci[35];
    o->pl0_rw_mode_mask    = get16le(ci + 36);
    o->pl0_addr_map_mask   = get16le(ci + 38);
    o->msg_length_c2d      = get16le(ci + 40);
    o->msg_length_d2c      = get16le(ci + 42);
    o->msg_num_c2d         = ci[44];
    o->msg_num_d2c         = ci[45];
    memcpy(o->identifier, ci + 48, 64);
    o->identifier[64] = 0;
    o->device_id_hash      = get32le(ci + 112);
    memcpy(o->hash_str, ci + 116, 8);
    o->hash_str[8] = 0;
}

int tas_session_start(tas_conn *c, const char *identifier,
                      tas_con_info *info, uint8_t *tas_err)
{
    uint8_t p[112];
    uint8_t r[4096];
    int rl = 0;

    memset(p, 0, sizeof(p));
    p[0] = 0x1B;                        /* wl = 112/4 - 1            */
    p[1] = 0xA1;                        /* SESSION_START             */
    p[2] = 0x00;                        /* con_id (assigned by server) */
    p[3] = TAS_CLIENT_TYPE_RW;
    if (identifier != NULL)
        strncpy((char *)p + 16, identifier, 63);

    if (tas_send_raw(c, p, (int)sizeof(p)) != 0)
        return -1;
    if (tas_recv_raw(c, r, (int)sizeof(r), &rl) != 0)
        return -1;
    /* response: [wl=0x20][0xA1][con_id][err][vmin][vmax][ninst][0] + 124B con info */
    if (rl < 8 || r[1] != 0xA1)
        return -2;
    *tas_err = r[3];
    if (r[3] == TAS_ERR_OK && info != NULL && rl >= 132)
        parse_con_info(r + 8, info);
    return 0;
}

int tas_ping_con(tas_conn *c, tas_con_info *info, uint8_t *tas_err)
{
    uint8_t p[4] = { 0x00, 0xA2, 0x00, 0x00 };  /* [wl=0][PING][0][0] */
    uint8_t r[4096];
    int rl = 0;

    if (tas_send_raw(c, p, 4) != 0)
        return -1;
    if (tas_recv_raw(c, r, (int)sizeof(r), &rl) != 0)
        return -1;
    if (rl < 4 || r[1] != 0xA2)
        return -2;
    *tas_err = r[3];
    if (r[3] == TAS_ERR_OK && info != NULL && rl >= 132)
        parse_con_info(r + 8, info);
    return 0;
}

int tas_set_device_key(tas_conn *c, uint16_t unlock_option,
                       const uint8_t *key, uint16_t key_len,
                       uint8_t *tas_err)
{
    uint8_t p[8 + 1024];
    uint8_t r[256];
    int rl = 0;
    int words;
    int len;

    if (key_len > 1024)
        return -4;
    words = (key_len + 3) / 4;
    len = 8 + words * 4;

    memset(p, 0, sizeof(p));
    p[0] = (uint8_t)(1 + words);        /* wl                       */
    p[1] = 0xAD;                        /* SET_DEVICE_KEY           */
    p[2] = 0x00;                        /* con_id                   */
    p[3] = 0x00;
    put16le(p + 4, unlock_option);
    put16le(p + 6, key_len);
    if (key != NULL && key_len > 0)
        memcpy(p + 8, key, key_len);

    if (tas_send_raw(c, p, len) != 0)
        return -1;
    if (tas_recv_raw(c, r, (int)sizeof(r), &rl) != 0)
        return -1;
    if (rl < 4 || r[1] != 0xAD)
        return -2;
    *tas_err = r[3];
    return 0;
}

int tas_device_connect(tas_conn *c, uint16_t option,
                       uint16_t *feat_used, uint32_t *device_type,
                       uint8_t *tas_err)
{
    uint8_t p[8];
    uint8_t r[256];
    int rl = 0;

    memset(p, 0, sizeof(p));
    p[0] = 0x01;                        /* wl                       */
    p[1] = 0xA8;                        /* DEVICE_CONNECT           */
    p[2] = 0x00;                        /* con_id                   */
    p[3] = 0x00;
    put16le(p + 4, option);

    if (tas_send_raw(c, p, (int)sizeof(p)) != 0)
        return -1;
    if (tas_recv_raw(c, r, (int)sizeof(r), &rl) != 0)
        return -1;
    /* response: [wl=2][0xA8][con_id][err][feat_used u16][rsvd u16][device_type u32] */
    if (rl < 4 || r[1] != 0xA8)
        return -2;
    *tas_err = r[3];
    if (r[3] == TAS_ERR_OK && rl >= 12) {
        if (feat_used != NULL)
            *feat_used = get16le(r + 4);
        if (device_type != NULL)
            *device_type = get32le(r + 8);
    }
    return 0;
}

/* Number of data words a PL0 element carries (wl semantics: 0 == 256). */
static uint32_t rd_words(uint32_t num_bytes) { return (num_bytes + 3) / 4; }

static uint8_t rd_cmd_byte(uint32_t num_bytes)
{
    switch (num_bytes) {
    case 1:  return 0x21;   /* RD8  */
    case 2:  return 0x23;   /* RD16 */
    case 4:  return 0x25;   /* RD32 */
    default: return 0x27;   /* RD64 */
    }
}

static uint8_t wr_cmd_byte(uint32_t num_bytes)
{
    switch (num_bytes) {
    case 1:  return 0x20;   /* WR8  */
    case 2:  return 0x22;   /* WR16 */
    case 4:  return 0x24;   /* WR32 */
    default: return 0x26;   /* WR64 */
    }
}

int tas_rw_exec(tas_conn *c, tas_rw_item *items, uint32_t n_items,
                uint32_t max_num_rw, uint8_t *pl0_err)
{
    uint32_t maxrq = 1060, maxrsp = 1040;
    uint32_t idx = 0;
    uint8_t rq[1400];
    uint8_t rsp[2400];
    int rc = 0;

    if (max_num_rw == 0 || max_num_rw > 255)
        max_num_rw = 32;
    if (pl0_err != NULL)
        *pl0_err = 0;

    while (idx < n_items && rc == 0) {
        size_t wi = 4;                       /* leave room for PL2 length  */
        size_t start_off = wi;
        uint16_t mask = 0;
        uint8_t  cur_map = TAS_AM0;
        uint64_t base = 0;
        uint32_t frame_items = 0;
        size_t   rsp_need = 8;               /* START + END responses      */
        uint32_t first = idx;

        wi += 8;                             /* START element placeholder  */

        /* ---- build one PL2 packet (PL1 frame) ---- */
        while (idx < n_items) {
            tas_rw_item *it = &items[idx];
            size_t ctx = 0;
            size_t ib;
            size_t rs;
            uint64_t nbase;

            if (it->is_write) {
                if (it->num_bytes <= 4) {
                    ib = 8;                  /* WR8/16/32                 */
                } else if (it->num_bytes <= 8) {
                    ib = 12;                 /* WR64                      */
                } else if (it->num_bytes <= 1024 && (it->num_bytes % 4) == 0 &&
                           (it->addr % 4) == 0) {
                    ib = 4 + ((it->num_bytes + 3) & ~(size_t)3);  /* WRBLK */
                } else {
                    rc = -4;
                    goto done;
                }
                rs = 4;
            } else {
                if (it->num_bytes <= 8) {
                    ib = 4;                  /* RD8/16/32/64              */
                    rs = 4 + (size_t)rd_words(it->num_bytes) * 4;
                } else if (it->num_bytes <= 1024 && (it->num_bytes % 8) == 0 &&
                           (it->addr % 8) == 0) {
                    ib = 8;                  /* RDBLK                     */
                    rs = 4 + (size_t)rd_words(it->num_bytes) * 4;
                } else {
                    rc = -4;
                    goto done;
                }
            }

            nbase = it->addr & ~(uint64_t)0xFFFF;
            if (it->addr_map != cur_map)
                ctx += 4;
            if (it->addr < base || it->addr > base + 0xFFFF)
                ctx += (nbase < 0x100000000ull) ? 4 : 8;

            if (frame_items > 0) {
                if (frame_items >= max_num_rw ||
                    wi + ctx + ib + 4 > maxrq ||
                    rsp_need + rs > maxrsp)
                    break;                   /* finalize, retry in next frame */
            }

            /* address map changes first (routes the packet in the server) */
            if (it->addr_map != cur_map) {
                rq[wi + 0] = 0x00;
                rq[wi + 1] = 0x11;           /* PL0_ADDR_MAP              */
                rq[wi + 2] = it->addr_map;
                rq[wi + 3] = 0x00;
                cur_map = it->addr_map;
                wi += 4;
            }
            mask |= (uint16_t)(1u << cur_map);

            if (it->addr < base || it->addr > base + 0xFFFF) {
                if (nbase < 0x100000000ull) {
                    rq[wi + 0] = 0x00;
                    rq[wi + 1] = 0x15;       /* PL0_BASE_ADDR32           */
                    put16le(rq + wi + 2, (uint16_t)(nbase >> 16));
                    wi += 4;
                } else {
                    rq[wi + 0] = 0x01;
                    rq[wi + 1] = 0x16;       /* PL0_BASE_ADDR64           */
                    put16le(rq + wi + 2, (uint16_t)(nbase >> 16));
                    put32le(rq + wi + 4, (uint32_t)(nbase >> 32));
                    wi += 8;
                }
                base = nbase;
            }

            {
                uint16_t off16 = (uint16_t)(it->addr & 0xFFFFu);
                if (it->is_write) {
                    uint32_t n = it->num_bytes;
                    if (n <= 4) {
                        uint32_t v = 0;
                        memcpy(&v, it->data, n);
                        rq[wi + 0] = 0x01;
                        rq[wi + 1] = wr_cmd_byte(n);
                        put16le(rq + wi + 2, off16);
                        put32le(rq + wi + 4, v);
                        wi += 8;
                    } else if (n <= 8) {
                        rq[wi + 0] = 0x02;
                        rq[wi + 1] = 0x26;   /* WR64                      */
                        put16le(rq + wi + 2, off16);
                        memcpy(rq + wi + 4, it->data, 8);
                        wi += 12;
                    } else {
                        uint32_t words = n / 4;
                        rq[wi + 0] = (uint8_t)((words == 256) ? 0 : words);
                        rq[wi + 1] = 0x2A;   /* WRBLK                     */
                        put16le(rq + wi + 2, off16);
                        memcpy(rq + wi + 4, it->data, n);
                        wi += 4 + words * 4;
                    }
                } else {
                    uint32_t n = it->num_bytes;
                    if (n <= 8) {
                        rq[wi + 0] = 0x00;
                        rq[wi + 1] = rd_cmd_byte(n);
                        put16le(rq + wi + 2, off16);
                        wi += 4;
                    } else {
                        uint32_t words = n / 4;
                        rq[wi + 0] = 0x01;
                        rq[wi + 1] = 0x2C;   /* RDBLK                     */
                        put16le(rq + wi + 2, off16);
                        rq[wi + 4] = (uint8_t)((words == 256) ? 0 : words);
                        rq[wi + 5] = 0x00;
                        rq[wi + 6] = 0x00;
                        rq[wi + 7] = 0x00;
                        wi += 8;
                    }
                }
            }

            rsp_need += rs;
            frame_items++;
            idx++;
        }

        if (frame_items == 0) {              /* cannot happen, but guard  */
            rc = -4;
            break;
        }

        /* ---- PL1 START / END ---- */
        c->pl1_cnt++;
        rq[start_off + 0] = 0x01;
        rq[start_off + 1] = 0xB0;            /* PL0_START                 */
        rq[start_off + 2] = 0xFF;            /* con_id (reference client) */
        rq[start_off + 3] = 0x00;            /* protoc_ver (tas rw = 0)   */
        put16le(rq + start_off + 4, c->pl1_cnt);
        put16le(rq + start_off + 6, mask);

        rq[wi + 0] = 0x00;
        rq[wi + 1] = 0xB1;                   /* PL0_END                   */
        put16le(rq + wi + 2, (uint16_t)frame_items);
        wi += 4;

        /* PL2 framing: tas_send_raw prepends [len+4], which equals the
         * PL2 total size of our frame. */
        if (tas_send_raw(c, rq + 4, (int)wi - 4) != 0) {
            rc = -1;
            break;
        }
        if (getenv("MCD_TAS_LOG") != NULL) {
            size_t k;
            fprintf(stderr, "[rw] rq cnt %u frame_items %u:", (unsigned)c->pl1_cnt,
                    (unsigned)frame_items);
            for (k = 4; k < wi; k++)
                fprintf(stderr, " %02X", rq[k]);
            fprintf(stderr, "\n");
        }

        /* ---- response ---- */
        {
            int rl = 0;
            size_t P = 4;
            uint32_t j;
            uint8_t ferr;

            if (tas_recv_raw(c, rsp, (int)sizeof(rsp), &rl) != 0) {
                rc = -1;
                break;
            }
            if (getenv("MCD_TAS_LOG") != NULL) {
                int k;
                fprintf(stderr, "[rw] rsp %d bytes:", rl);
                for (k = 0; k < rl && k < 300; k++)
                    fprintf(stderr, " %02X", rsp[k]);
                fprintf(stderr, "%s\n", (rl > 300) ? " ..." : "");
            }
            if (rl < 8 || rsp[0] != 0x00 || rsp[1] != 0xB0) {
                rc = -2;
                break;
            }
            ferr = rsp[3];
            if (ferr != TAS_ERR_OK) {
                for (j = 0; j < frame_items; j++) {
                    tas_rw_item *it = &items[first + j];
                    it->ok = 0;
                    it->num_bytes_ok = 0;
                    it->pl_err = ferr;
                }
                if (pl0_err != NULL)
                    *pl0_err = ferr;
                rc = -3;
                break;
            }

            for (j = 0; j < frame_items; j++) {
                tas_rw_item *it = &items[first + j];
                uint8_t wl, cmd, wlrd, err;

                if (P + 4 > (size_t)rl) {
                    rc = -2;
                    break;
                }
                wl   = rsp[P + 0];
                cmd  = rsp[P + 1];
                wlrd = rsp[P + 2];
                err  = rsp[P + 3];
                (void)cmd;

                it->pl_err = err;
                if (it->is_write) {
                    uint32_t expw = (it->num_bytes + 3) / 4;
                    it->ok = (err == TAS_PL0_OK && wlrd == (uint8_t)(expw & 0xFF));
                    it->num_bytes_ok = it->ok ? it->num_bytes : 0;
                    P += 4;
                } else {
                    uint32_t datab;
                    if (cmd == 0x2D)                     /* RDBLK1KB ok   */
                        datab = 1024;
                    else if (cmd == 0x2C && wlrd == 0)   /* 256 words     */
                        datab = 1024;
                    else
                        datab = (uint32_t)wl * 4;
                    if (P + 4 + datab > (size_t)rl) {
                        rc = -2;
                        break;
                    }
                    if (err == TAS_PL0_OK) {
                        uint32_t copy = it->num_bytes;
                        if (copy > datab)
                            copy = datab;
                        memcpy(it->data, rsp + P + 4, copy);
                        it->num_bytes_ok = copy;
                        it->ok = (copy == it->num_bytes);
                    } else {
                        it->num_bytes_ok = 0;
                        it->ok = 0;
                    }
                    P += 4 + datab;
                }
                if (rc != 0)
                    break;
            }
            if (rc != 0)
                break;

            if (P + 4 > (size_t)rl || rsp[P + 0] != 0x00 || rsp[P + 1] != 0xB1) {
                rc = -2;
                break;
            }
            if (get16le(rsp + P + 2) != c->pl1_cnt) {
                rc = -2;
                break;
            }
        }
    }

done:
    return rc;
}
