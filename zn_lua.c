#define LUA_LIB
#define LBIND_STATIC_API
#define LBIND_DEFAULT_FLAG (LBIND_TRACK|LBIND_ACCESSOR)
#include "lbind.h"

#define ZN_STATIC_API
#include "znet.h"
#include "zn_buffer.h"

#define LZN_UDP_RECVSIZE 2048

LBIND_TYPE(lbT_State, "znet.State");
LBIND_TYPE(lbT_Timer, "znet.Timer");
LBIND_TYPE(lbT_Accept, "znet.Accept");
LBIND_TYPE(lbT_Tcp, "znet.Tcp");
LBIND_TYPE(lbT_Udp, "znet.Udp");


/* utils */

#define return_result(L, err) do {       \
    if (err == ZN_OK) lbind_returnself(L);    \
    else return lzn_pushresult(L, err); } while (0)

static int lzn_pushresult(lua_State *L, int err) {
    lua_pushnil(L);
    lua_pushstring(L, zn_strerror(err));
    lua_pushinteger(L, err);
    return 3;
}

static lua_Integer lzn_posrelat(lua_Integer pos, size_t len) {
  if (pos >= 0) return pos;
  else if (0u - (size_t)pos > len) return 0;
  else return (lua_Integer)len + pos + 1;
}

static void lzn_ref(lua_State *L, int idx, int *ref) {
    lua_pushvalue(L, idx);
    if (*ref == LUA_NOREF)
        *ref = luaL_ref(L, LUA_REGISTRYINDEX);
    else
        lua_rawseti(L, LUA_REGISTRYINDEX, *ref);
}

static void lzn_unref(lua_State *L, int *ref) {
    luaL_unref(L, LUA_REGISTRYINDEX, *ref);
    *ref = LUA_NOREF;
}


/* znet timer */

typedef struct lzn_Timer {
    zn_Timer *timer;
    lua_State *L;
    int ontimer_ref;
    int ref;
    unsigned delayms;
} lzn_Timer;

static zn_Time lzn_ontimer(void *ud, zn_Timer *timer, zn_Time elapsed) {
    lzn_Timer *obj = (lzn_Timer*)ud;
    lua_State *L = obj->L;
    lua_rawgeti(L, LUA_REGISTRYINDEX, obj->ontimer_ref);
    lua_rawgeti(L, LUA_REGISTRYINDEX, obj->ref);
    lua_pushinteger(L, (lua_Integer)elapsed);
    if (lbind_pcall(L, 2, 1) != LUA_OK) {
        fprintf(stderr, "%s\n", lua_tostring(L, -1));
        lzn_unref(L, &obj->ref);
    }
    else if (lua_isinteger(L, -1)) {
        lua_Integer ret = lua_tointeger(L, -1);
        return ret >= 0 ? (zn_Time)ret : 0;
    }
    else if (lua_toboolean(L, -1))
        zn_starttimer(timer, obj->delayms);
    else
        lzn_unref(L, &obj->ref);
    lua_pop(L, 1);
    return 0;
}

static int Ltimer_new(lua_State *L) {
    zn_State *S = (zn_State*)lbind_check(L, 1, &lbT_State);
    lzn_Timer *obj;
    luaL_checktype(L, 2, LUA_TFUNCTION);
    obj = (lzn_Timer*)lbind_new(L, sizeof(lzn_Timer), &lbT_Timer);
    obj->timer = zn_newtimer(S, lzn_ontimer, obj);
    obj->L = L;
    obj->ontimer_ref = LUA_NOREF;
    obj->ref = LUA_NOREF;
    if (obj->timer != NULL) {
        lzn_ref(L, 2, &obj->ontimer_ref);
        return 1;
    }
    return 0;
}

static int Ltimer_delete(lua_State *L) {
    lzn_Timer *obj = (lzn_Timer*)lbind_test(L, 1, &lbT_Timer);
    if (obj->timer != NULL) {
        zn_deltimer(obj->timer);
        lbind_delete(L, 1);
        lzn_unref(L, &obj->ontimer_ref);
        lzn_unref(L, &obj->ref);
        obj->timer = NULL;
    }
    return 0;
}

static int Ltimer_start(lua_State *L) {
    lzn_Timer *obj = (lzn_Timer*)lbind_check(L, 1, &lbT_Timer);
    lua_Integer delayms = luaL_optinteger(L, 2, 0);
    if (!obj->timer) return 0;
    if (delayms < 0) delayms = 0;
    obj->delayms = (unsigned)delayms;
    if (zn_starttimer(obj->timer, obj->delayms))
        lzn_ref(L, 1, &obj->ref);
    lbind_returnself(L);
}

static int Ltimer_cancel(lua_State *L) {
    lzn_Timer *obj = (lzn_Timer*)lbind_check(L, 1, &lbT_Timer);
    if (!obj->timer) return 0;
    lzn_unref(L, &obj->ref);
    zn_canceltimer(obj->timer);
    lbind_returnself(L);
}

static void open_timer(lua_State *L) {
    luaL_Reg libs[] = {
#define ENTRY(name) { #name, Ltimer_##name }
        ENTRY(new),
        ENTRY(delete),
        ENTRY(start),
        ENTRY(cancel),
#undef  ENTRY
        { NULL, NULL }
    };
    lbind_newmetatable(L, libs, &lbT_Timer);
}


/* znet tcp */

typedef struct lzn_Tcp {
    zn_Tcp *tcp;
    lua_State *L;
    int onconnect_ref;
    int onheader_ref;
    int onpacket_ref;
    int onerror_ref;
    int ref;
    int closing;
    zn_SendBuffer send;
    zn_RecvBuffer recv;
} lzn_Tcp;

static void lzn_freetcp(lua_State *L, lzn_Tcp *obj) {
    zn_resetsendbuffer(&obj->send);
    zn_resetrecvbuffer(&obj->recv);
    lzn_unref(L, &obj->onconnect_ref);
    lzn_unref(L, &obj->onheader_ref);
    lzn_unref(L, &obj->onpacket_ref);
    lzn_unref(L, &obj->onerror_ref);
    lzn_unref(L, &obj->ref);
    obj->tcp = NULL;
    obj->closing = 1;
}

static void lzn_onconnect(void *ud, zn_Tcp *tcp, unsigned err) {
    lzn_Tcp *obj = (lzn_Tcp*)ud;
    lua_State *L = obj->L;
    (void)tcp;
    lua_rawgeti(L, LUA_REGISTRYINDEX, obj->onconnect_ref);
    lua_rawgeti(L, LUA_REGISTRYINDEX, obj->ref);
    lzn_unref(L, &obj->ref);
    if (err != ZN_OK) {
        lua_pushstring(L, zn_strerror(err));
        lua_pushinteger(L, err);
    }
    if (lbind_pcall(L, err == ZN_OK ? 1 : 3, 0) != LUA_OK) {
        fprintf(stderr, "%s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

static void lzn_tcperror(lua_State *L, lzn_Tcp *obj, int err) {
    if (obj->onerror_ref != LUA_NOREF) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, obj->onerror_ref);
        lua_rawgeti(L, LUA_REGISTRYINDEX, obj->ref);
        lua_pushstring(L, zn_strerror(err));
        lua_pushinteger(L, err);
        if (lbind_pcall(L, 3, 0) != LUA_OK) {
            fprintf(stderr, "%s\n", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }
    if (obj->tcp) {
        zn_deltcp(obj->tcp);
        lzn_freetcp(L, obj);
    }
}

static size_t lzn_onheader(void *ud, const char *buff, size_t len) {
    lzn_Tcp *obj = (lzn_Tcp*)ud;
    lua_State *L = obj->L;
    size_t ret = len;
    lua_rawgeti(L, LUA_REGISTRYINDEX, obj->onheader_ref);
    lua_rawgeti(L, LUA_REGISTRYINDEX, obj->ref);
    lua_pushlstring(L, buff, len);
    if (lbind_pcall(L, 2, 1) != LUA_OK) {
        fprintf(stderr, "%s\n", lua_tostring(L, -1));
        lzn_unref(L, &obj->ref);
    }
    else if (lua_isinteger(L, -1))
        ret = (size_t)lua_tointeger(L, -1);
    lua_pop(L, 1);
    return ret;
}

static void lzn_onpacket(void *ud, const char *buff, size_t len) {
    lzn_Tcp *obj = (lzn_Tcp*)ud;
    lua_State *L = obj->L;
    if (obj->onpacket_ref == LUA_NOREF) return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, obj->onpacket_ref);
    lua_rawgeti(L, LUA_REGISTRYINDEX, obj->ref);
    lua_pushlstring(L, buff, len);
    if (lbind_pcall(L, 2, 0) != LUA_OK) {
        fprintf(stderr, "%s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

static void lzn_onsend(void *ud, zn_Tcp *tcp, unsigned err, unsigned count) {
    lzn_Tcp *obj = (lzn_Tcp*)ud;
    if (err == ZN_OK) {
        if (!zn_sendfinish(&obj->send, count))
            goto check_close;
        err = zn_send(tcp,
                zn_sendbuff(&obj->send),
                zn_sendsize(&obj->send), lzn_onsend, obj);
    }
    if (err != ZN_OK) lzn_tcperror(obj->L, obj, err);
check_close:
    if (obj->closing && zn_sendsize(&obj->send) == 0) {
        zn_closetcp(tcp);
        lzn_freetcp(obj->L, obj);
    }
}

static void lzn_onrecv(void *ud, zn_Tcp *tcp, unsigned err, unsigned count) {
    lzn_Tcp *obj = (lzn_Tcp*)ud;
    if (err == ZN_OK) {
        zn_recvfinish(&obj->recv, count);
        err = zn_recv(tcp,
                zn_recvbuff(&obj->recv),
                zn_recvsize(&obj->recv), lzn_onrecv, obj);
    }
    if (err != ZN_OK) lzn_tcperror(obj->L, obj, err);
}

static lzn_Tcp *lzn_newtcp(lua_State *L, zn_Tcp *tcp) {
    lzn_Tcp *obj = (lzn_Tcp*)lbind_new(L, sizeof(lzn_Tcp), &lbT_Tcp);
    obj->tcp = tcp;
    obj->L = L;
    obj->onconnect_ref = LUA_NOREF;
    obj->onheader_ref = LUA_NOREF;
    obj->onpacket_ref = LUA_NOREF;
    obj->onerror_ref = LUA_NOREF;
    obj->ref = LUA_NOREF;
    obj->closing = 0;
    zn_initsendbuffer(&obj->send);
    zn_initrecvbuffer(&obj->recv);
    zn_recvonheader(&obj->recv, lzn_onheader, obj);
    zn_recvonpacket(&obj->recv, lzn_onpacket, obj);
    return obj;
}

static int Ltcp_connect(lua_State *L) {
    lzn_Tcp *obj = (lzn_Tcp*)lbind_check(L, 1, &lbT_Tcp);
    const char *addr = luaL_optstring(L, 2, "127.0.0.1");
    lua_Integer port = luaL_checkinteger(L, 3);
    int ret;
    luaL_checktype(L, 4, LUA_TFUNCTION);
    luaL_argcheck(L, port > 0, 3, "port out of range");
    lzn_ref(L, 4, &obj->onconnect_ref);
    lzn_ref(L, 1, &obj->ref);
    ret = zn_connect(obj->tcp, addr, (unsigned)port, lzn_onconnect, obj);
    return_result(L, ret);
}

static int Ltcp_new(lua_State *L) {
    zn_State *S = (zn_State*)lbind_check(L, 1, &lbT_State);
    int top = lua_gettop(L);
    zn_Tcp *tcp;
    if ((tcp = zn_newtcp(S)) == NULL)
        return 0;
    lzn_newtcp(L, tcp);
    if (top != 1) {
        lua_replace(L, 1);
        return Ltcp_connect(L);
    }
    return 1;
}

static int Ltcp_delete(lua_State *L) {
    lzn_Tcp *obj = (lzn_Tcp*)lbind_test(L, 1, &lbT_Tcp);
    if (obj && obj->tcp != NULL) {
        obj->closing = 1;
        lzn_unref(L, &obj->onerror_ref);
        if (zn_sendsize(&obj->send) == 0) {
            zn_deltcp(obj->tcp);
            lzn_freetcp(L, obj);
            lbind_delete(L, 1);
        }
    }
    return 0;
}

static int Ltcp_onerror(lua_State *L) {
    lzn_Tcp *obj = (lzn_Tcp*)lbind_check(L, 1, &lbT_Tcp);
    if (!obj->tcp) return 0;
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lzn_ref(L, 2, &obj->onerror_ref);
    lbind_returnself(L);
}

static int Ltcp_send(lua_State *L) {
    lzn_Tcp *obj = (lzn_Tcp*)lbind_check(L, 1, &lbT_Tcp);
    size_t len;
    const char *data = luaL_checklstring(L, 2, &len);
    lua_Integer i = lzn_posrelat(luaL_optinteger(L, 3, 1), len);
    lua_Integer j = lzn_posrelat(luaL_optinteger(L, 4, len), len);
    int ret = ZN_OK;
    len = (size_t)(i > j ? 0 : j - i + 1);
    if (!obj->tcp || obj->closing) return 0;
    if (!zn_sendprepare(&obj->send, data + i - 1, len))
        lbind_returnself(L);
    lzn_ref(L, 1, &obj->ref);
    ret = zn_send(obj->tcp,
            zn_sendbuff(&obj->send),
            zn_sendsize(&obj->send), lzn_onsend, obj);
    return_result(L, ret);
}

static int Ltcp_receive(lua_State *L) {
    lzn_Tcp *obj = (lzn_Tcp*)lbind_check(L, 1, &lbT_Tcp);
    int ret;
    if (!obj->tcp) return 0;
    luaL_checktype(L, 2, LUA_TFUNCTION);
    if (lua_isfunction(L, 3))
        lzn_ref(L, 3, &obj->onpacket_ref);
    lzn_ref(L, 2, &obj->onheader_ref);
    lzn_ref(L, 1, &obj->ref);
    ret = zn_recv(obj->tcp,
            zn_recvbuff(&obj->recv),
            zn_recvsize(&obj->recv), lzn_onrecv, obj);
    return_result(L, ret);
}

static int Ltcp_close(lua_State *L) {
    lzn_Tcp *obj = (lzn_Tcp*)lbind_check(L, 1, &lbT_Tcp);
    if (obj->tcp) {
        obj->closing = 1;
        if (zn_sendsize(&obj->send) == 0) {
            int ret = zn_closetcp(obj->tcp);
            lzn_freetcp(L, obj);
            return_result(L, ret);
        }
    }
    lbind_returnself(L);
}

static int Ltcp_getpeerinfo(lua_State *L) {
    lzn_Tcp *obj = (lzn_Tcp*)lbind_check(L, 1, &lbT_Tcp);
    zn_PeerInfo info;
    zn_getpeerinfo(obj->tcp, &info);
    lua_pushstring(L, info.addr);
    lua_pushinteger(L, info.port);
    return 2;
}

static void open_tcp(lua_State *L) {
    luaL_Reg libs[] = {
        { "__gc", Ltcp_delete },
#define ENTRY(name) { #name, Ltcp_##name }
        ENTRY(new),
        ENTRY(delete),
        ENTRY(close),
        ENTRY(connect),
        ENTRY(send),
        ENTRY(receive),
        ENTRY(getpeerinfo),
        ENTRY(onerror),
#undef  ENTRY
        { NULL, NULL }
    };
    lbind_newmetatable(L, libs, &lbT_Tcp);
}


/* znet accept */

typedef struct lzn_Accept {
    zn_Accept *accept;
    lua_State *L;
    int onaccept_ref;
    int ref;
} lzn_Accept;

static void lzn_onaccept(void *ud, zn_Accept *accept, unsigned err, zn_Tcp *tcp) {
    lzn_Accept *obj = (lzn_Accept*)ud;
    lua_State *L = obj->L;
    if (err != ZN_OK) return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, obj->onaccept_ref);
    lua_rawgeti(L, LUA_REGISTRYINDEX, obj->ref);
    lzn_newtcp(L, tcp);
    if (lbind_pcall(L, 2, 1) != LUA_OK) {
        fprintf(stderr, "%s\n", lua_tostring(L, -1));
        lzn_unref(L, &obj->ref);
    }
    else if (lua_toboolean(L, -1))
        zn_accept(accept, lzn_onaccept, ud);
    else lzn_unref(L, &obj->ref);
    lua_pop(L, 1);
}

static void lzn_freeaccept(lua_State *L, lzn_Accept *obj) {
    lzn_unref(L, &obj->onaccept_ref);
    lzn_unref(L, &obj->ref);
    obj->accept = NULL;
}

static int Laccept_new(lua_State *L) {
    zn_State *S = (zn_State*)lbind_check(L, 1, &lbT_State);
    lzn_Accept *obj;
    luaL_checktype(L, 2, LUA_TFUNCTION);
    obj = (lzn_Accept*)lbind_new(L, sizeof(lzn_Accept), &lbT_Accept);
    obj->L = L;
    obj->accept = zn_newaccept(S);
    obj->onaccept_ref = LUA_NOREF;
    obj->ref = LUA_NOREF;
    if (obj->accept != NULL) {
        lzn_ref(L, 2, &obj->onaccept_ref);
        return 1;
    }
    return 0;
}

static int Laccept_delete(lua_State *L) {
    lzn_Accept *obj = (lzn_Accept*)lbind_test(L, 1, &lbT_Accept);
    if (obj && obj->accept != NULL) {
        zn_delaccept(obj->accept);
        lzn_freeaccept(L, obj);
        lbind_delete(L, 1);
    }
    return 0;
}

static int Laccept_listen(lua_State *L) {
    lzn_Accept *obj = (lzn_Accept*)lbind_check(L, 1, &lbT_Accept);
    const char *addr = luaL_optstring(L, 2, "127.0.0.1");
    lua_Integer port = luaL_checkinteger(L, 3);
    int ret;
    if (!obj->accept) return 0;
    if (port < 0) luaL_argerror(L, 2, "port out of range");
    if ((ret = zn_listen(obj->accept, addr, (unsigned)port)) == ZN_OK)
        ret = zn_accept(obj->accept, lzn_onaccept, obj);
    if (ret == ZN_OK) lzn_ref(L, 1, &obj->ref);
    return_result(L, ret);
}

static int Laccept_close(lua_State *L) {
    lzn_Accept *obj = (lzn_Accept*)lbind_check(L, 1, &lbT_Accept);
    int ret;
    if (!obj->accept) return 0;
    ret = zn_closeaccept(obj->accept);
    lzn_freeaccept(L, obj);
    return_result(L, ret);
}

static void open_accept(lua_State *L) {
    luaL_Reg libs[] = {
        { "__gc", Laccept_delete },
#define ENTRY(name) { #name, Laccept_##name }
        ENTRY(new),
        ENTRY(delete),
        ENTRY(listen),
        ENTRY(close),
#undef  ENTRY
        { NULL, NULL }
    };
    lbind_newmetatable(L, libs, &lbT_Accept);
}


/* znet udp */

typedef struct lzn_Udp {
    zn_Udp *udp;
    lua_State *L;
    int onrecvfrom_ref;
    int ref;
    zn_Buffer recv;
} lzn_Udp;

static void lzn_onrecvfrom(void *ud, zn_Udp *udp, unsigned err, unsigned count, const char *addr, unsigned port) {
    lzn_Udp *obj = (lzn_Udp*)ud;
    lua_State *L = obj->L;
    lua_rawgeti(L, LUA_REGISTRYINDEX, obj->onrecvfrom_ref);
    lua_rawgeti(L, LUA_REGISTRYINDEX, obj->ref);
    if (err == ZN_OK) {
        lua_pushlstring(L, zn_buffer(&obj->recv), count);
        zn_bufflen(&obj->recv) = 0;
        lua_pushstring(L, addr);
        lua_pushinteger(L, port);
    }
    else {
        lua_pushnil(L);
        lua_pushstring(L, zn_strerror(err));
        lua_pushinteger(L, err);
    }
    if (lbind_pcall(L, 4, 1) != LUA_OK) {
        fprintf(stderr, "%s\n", lua_tostring(L, -1));
        lzn_unref(L, &obj->ref);
    }
    else if (lua_toboolean(L, -1)) {
        int ret = zn_recvfrom(udp,
                zn_prepbuffsize(&obj->recv, LZN_UDP_RECVSIZE),
                LZN_UDP_RECVSIZE, lzn_onrecvfrom, obj);
        if (ret != ZN_OK)
            lzn_onrecvfrom(ud, udp, ret, 0, NULL, 0);
    }
    else lzn_unref(L, &obj->ref);
    lua_pop(L, 1);
}

static int Ludp_new(lua_State *L) {
    zn_State *S = (zn_State*)lbind_check(L, 1, &lbT_State);
    const char *addr = luaL_optstring(L, 2, "127.0.0.1");
    lua_Integer port = luaL_optinteger(L, 3, 0);
    lzn_Udp *obj;
    luaL_checktype(L, 2, LUA_TFUNCTION);
    if (port < 0) luaL_argerror(L, 2, "port out of range");
    obj = (lzn_Udp*)lbind_new(L, sizeof(lzn_Udp), &lbT_Udp);
    obj->udp = zn_newudp(S, addr, (unsigned)port);
    obj->L = L;
    obj->onrecvfrom_ref = LUA_NOREF;
    obj->ref = LUA_NOREF;
    return 1;
}

static int Ludp_delete(lua_State *L) {
    lzn_Udp *obj = (lzn_Udp*)lbind_test(L, 1, &lbT_Udp);
    if (obj && obj->udp != NULL) {
        zn_deludp(obj->udp);
        lbind_delete(L, 1);
        lzn_unref(L, &obj->onrecvfrom_ref);
        lzn_unref(L, &obj->ref);
        obj->udp = NULL;
    }
    return 0;
}

static int Ludp_sendto(lua_State *L) {
    lzn_Udp *obj = (lzn_Udp*)lbind_check(L, 1, &lbT_Udp);
    const char *addr = luaL_checkstring(L, 2);
    lua_Integer port = luaL_checkinteger(L, 3);
    size_t len;
    const char *data = luaL_checklstring(L, 4, &len);
    lua_Integer i = lzn_posrelat(luaL_optinteger(L, 5, 1), len);
    lua_Integer j = lzn_posrelat(luaL_optinteger(L, 6, len), len);
    len = (size_t)(i > j ? 0 : j - i + 1);
    int ret = zn_sendto(obj->udp, data + i - 1, len, addr, (unsigned)port);
    return_result(L, ret);
}

static int Ludp_receivefrom(lua_State *L) {
    lzn_Udp *obj = (lzn_Udp*)lbind_check(L, 1, &lbT_Udp);
    int ret;
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lzn_ref(L, 2, &obj->onrecvfrom_ref);
    ret = zn_recvfrom(obj->udp,
            zn_prepbuffsize(&obj->recv, LZN_UDP_RECVSIZE),
            LZN_UDP_RECVSIZE, lzn_onrecvfrom, obj);
    if (ret == ZN_OK) lzn_ref(L, 1, &obj->ref);
    return_result(L, ret);
}

static void open_udp(lua_State *L) {
    luaL_Reg libs[] = {
        { "__gc", Ludp_delete },
#define ENTRY(name) { #name, Ludp_##name }
        ENTRY(new),
        ENTRY(delete),
        ENTRY(sendto),
        ENTRY(receivefrom),
#undef  ENTRY
        { NULL, NULL }
    };
    if (lbind_newmetatable(L, libs, &lbT_Udp))
        lbind_setaccessors(L, 0, LBIND_INDEX|LBIND_NEWINDEX);
}


/* znet state */

static int Lstate_new(lua_State *L) {
    zn_State *S = zn_newstate();
    if (S == NULL)
        return 0;
    lbind_wrap(L, S, &lbT_State);
    return 1;
}

static int Lstate_delete(lua_State *L) {
    zn_State *S = (zn_State*)lbind_test(L, 1, &lbT_State);
    if (S != NULL) {
        lbind_delete(L, 1);
        zn_close(S);
    }
    return 0;
}

static int Lstate_run(lua_State *L) {
    zn_State *S = (zn_State*)lbind_check(L, 1, &lbT_State);
    const char *s = luaL_optstring(L, 2, "loop");
    int ret;
    if (*s == 'c') /* check */
        ret = zn_run(S, ZN_RUN_CHECK);
    else if (*s == 'o')
        ret = zn_run(S, ZN_RUN_ONCE);
    else
        ret = zn_run(S, ZN_RUN_LOOP);
    return_result(L, ret);
}

static int lzn_deinitialize(lua_State *L) {
    (void)L;
    zn_deinitialize();
    return 0;
}

LUALIB_API int luaopen_znet(lua_State *L) {
    luaL_Reg libs[] = {
        { "timer",  Ltimer_new },
        { "accept", Laccept_new },
        { "tcp",    Ltcp_new },
        { "udp",    Ludp_new },
#define ENTRY(name) { #name, Lstate_##name }
        ENTRY(new),
        ENTRY(delete),
        ENTRY(run),
#undef  ENTRY
        { NULL, NULL }
    };
    lbind_install(L, NULL);
    open_timer(L);
    open_accept(L);
    open_tcp(L);
    open_udp(L);
    if (lbind_newmetatable(L, libs, &lbT_State)) {
        lbind_setaccessors(L, 0, LBIND_INDEX|LBIND_NEWINDEX);

        /* initialize and register deinitialize */
        zn_initialize();
        lua_newtable(L);
        lua_pushcfunction(L, lzn_deinitialize);
        lua_setfield(L, -2, "__gc");
        lua_setmetatable(L, -2);
    }
    lua_pushstring(L, zn_engine());
    lua_setfield(L, -2, "engine");
    return 1;
}
/* cc: flags+='-mdll -s -O3 -DLUA_BUILD_AS_DLL'
 * cc: libs+='-lws2_32 -llua53' output='znet.dll' */
