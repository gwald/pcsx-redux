/***************************************************************************
 *   Copyright (C) 2019 PCSX-Redux authors                                 *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.           *
 ***************************************************************************/

#include "lua/luawrapper.h"

#include <assert.h>

static int callwrap(lua_State* raw, lua_CFunction func) {
    PCSX::Lua L(raw);
    int r = 0;

    try {
        r = func(raw);
    } catch (std::exception& e) {
        L.error(std::string("LuaException: ") + e.what());
    }
    return r;
}

PCSX::Lua::Lua() : L(lua_open()) {
    assert(("Couldn't create Lua VM", L));
    lua_atpanic(L, [](lua_State* L) -> int { throw std::runtime_error(lua_tostring(L, 1)); });
    setCallWrap(callwrap);
    push("_THREADS");
    newtable();
    settable(LUA_REGISTRYINDEX);
}

PCSX::Lua& PCSX::Lua::operator=(Lua&& oL) noexcept {
    if (this == &oL) return *this;

    assert(("Can't assign a Lua VM to another one.", !L));

    L = oL.L;
    oL.L = nullptr;

    return *this;
}

void PCSX::Lua::close() {
    assert(("Can't close an already closed VM", L));

    lua_close(L);
    L = nullptr;
}

void PCSX::Lua::open_base() {
    int n = gettop();
    luaopen_base(L);
    while (n < gettop()) pop();
}

void PCSX::Lua::open_bit() {
    int n = gettop();
    luaopen_bit(L);
    while (n < gettop()) pop();
}

void PCSX::Lua::open_debug() {
    int n = gettop();
    luaopen_debug(L);
    while (n < gettop()) pop();
}

void PCSX::Lua::open_ffi() {
    int n = gettop();
    luaopen_ffi(L);
    push("ffi");
    copy(-2);
    settable(LUA_GLOBALSINDEX);
    while (n < gettop()) pop();
}

void PCSX::Lua::open_math() {
    int n = gettop();
    luaopen_math(L);
    while (n < gettop()) pop();
}

void PCSX::Lua::open_jit() {
    int n = gettop();
    luaopen_jit(L);
    while (n < gettop()) pop();
}

void PCSX::Lua::open_table() {
    int n = gettop();
    luaopen_table(L);
    while (n < gettop()) pop();
}

void PCSX::Lua::open_package() {
    int n = gettop();
    luaopen_package(L);
    while (n < gettop()) pop();
}

void PCSX::Lua::open_string() {
    int n = gettop();
    luaopen_string(L);
    while (n < gettop()) pop();
}

std::unique_ptr<PCSX::Lua> PCSX::Lua::thread(bool saveit) {
    checkstack();
    lua_State* L1 = lua_newthread(L);
    if (saveit) {                     // -1 = thread
        push("_THREADS");             // -2 = thread, -1 = "_THREADS"
        gettable(LUA_REGISTRYINDEX);  // -2 = thread, -1 = _THREADS
        push(L1);                     // -3 = thread, -2 = _THREADS, -1 = key-Lt
        copy(-3);                     // -4 = thread, -3 = _THREADS, -2 = key-Lt, -1 = thread
        settable();                   // -2 = thread, -1 = _THREADS
        pop();                        // -1 = thread
    }
    return std::make_unique<Lua>(L1);
}

void PCSX::Lua::weaken() {
    push("_THREADS");             // -1 = "_THREADS"
    gettable(LUA_REGISTRYINDEX);  // -1 = _THREADS
    push(L);                      // -2 = _THREADS, -1 = key-Lt
    push();                       // -3 = _THREADS, -2 = key-Lt, -1 = nil
    settable();                   // -1 = _THREADS
    pop();
}

void PCSX::Lua::setCallWrap(lua_CallWrapper wrapper) {
    push((void*)wrapper);
    luaJIT_setmode(L, -1, LUAJIT_MODE_WRAPCFUNC | LUAJIT_MODE_ON);
    pop();
}

void PCSX::Lua::declareFunc(const char* name, lua_CFunction f, int i) {
    checkstack(2);
    lua_pushstring(L, name);
    lua_pushcfunction(L, f);
    if ((i < 0) && (i > LUA_REGISTRYINDEX)) i += 2;
    lua_settable(L, i);
}

static std::function<int(PCSX::Lua)>* getLambda(PCSX::Lua L) {
    return reinterpret_cast<std::function<int(PCSX::Lua)>*>(L.touserdata(lua_upvalueindex(1)));
}

static int lambdaWrapper(lua_State* L_) {
    PCSX::Lua L(L_);
    auto* lambda = getLambda(L);
    return lambda->operator()(L);
}

static int lambdaGC(lua_State* L_) {
    PCSX::Lua L(L_);
    auto* lambda = getLambda(L);
    delete lambda;
    return 0;
}

void PCSX::Lua::declareFunc(const char* name, std::function<int(Lua)> f, int i) {
    checkstack(5);
    lua_pushstring(L, name);
    new (lua_newuserdata(L, sizeof(f))) std::function<int(Lua)>(f);
    newtable();
    push("__gc");
    push(lambdaGC);
    settable();
    setmetatable();
    lua_pushcclosure(L, lambdaWrapper, 1);
    if ((i < 0) && (i > LUA_REGISTRYINDEX)) i += 2;
    lua_settable(L, i);
}

void PCSX::Lua::call(const char* f, int i, int nargs) {
    checkstack(1);
    lua_pushstring(L, f);
    lua_gettable(L, i);
    lua_insert(L, -1 - nargs);
    call(nargs);
}

void PCSX::Lua::call(int nargs) {
    int r = lua_resume(L, nargs);

    if ((r == LUA_YIELD) || (r == 0)) return;

    pushLuaContext();
    displayStack(true);
    while (gettop()) pop();

    switch (r) {
        case LUA_ERRRUN:
            throw std::runtime_error("Runtime error while running LUA code.");
        case LUA_ERRMEM:
            throw std::runtime_error("Memory allocation error while running LUA code.");
        case LUA_ERRERR:
            throw std::runtime_error("Error in Error function.");
        case LUA_ERRSYNTAX:
            throw std::runtime_error("Syntax error in Lua code.");
        default:
            throw std::runtime_error(std::string("Unknow error while running LUA code (err code: ") +
                                     std::to_string(r) + ")");
    }
}

void PCSX::Lua::pcall(int nargs) {
    int r = lua_pcall(L, nargs, LUA_MULTRET, 0);

    if (r == 0) return;

    pushLuaContext();
    displayStack(true);
    while (gettop()) pop();

    switch (r) {
        case LUA_ERRRUN:
            throw std::runtime_error("Runtime error while running LUA code.");
        case LUA_ERRMEM:
            throw std::runtime_error("Memory allocation error while running LUA code.");
        case LUA_ERRERR:
            throw std::runtime_error("Error in Error function.");
        case LUA_ERRSYNTAX:
            throw std::runtime_error("Syntax error in Lua code.");
        default:
            throw std::runtime_error(std::string("Unknow error while running LUA code (err code: ") +
                                     std::to_string(r) + ")");
    }
}

void PCSX::Lua::settable(int i, bool raw) {
    if (raw) {
        lua_rawset(L, i);
    } else {
        lua_settable(L, i);
    }
}

void PCSX::Lua::gettable(int i, bool raw) {
    if (raw) {
        lua_rawget(L, i);
    } else {
        lua_gettable(L, i);
    }
}

void PCSX::Lua::getglobal(const char* name) {
    push(name);
    gettable(LUA_GLOBALSINDEX);
}

void PCSX::Lua::pushLuaContext() {
    std::string whole_msg;
    struct lua_Debug ar;
    bool got_error = false;
    int level = 0;

    do {
        if (lua_getstack(L, level, &ar) == 1) {
            if (lua_getinfo(L, "nSl", &ar) != 0) {
                push(std::string("at ") + ar.source + ":" + std::to_string(ar.currentline) + " (" +
                     (ar.name ? ar.name : "[top]") + ")");
            } else {
                got_error = true;
            }
        } else {
            got_error = true;
        }
        level++;
    } while (!got_error);
}

void PCSX::Lua::error(const char* msg) {
    push(msg);

    if (yielded()) {
        pushLuaContext();
        displayStack(true);
        while (gettop()) pop();

        throw std::runtime_error("Runtime error while running yielded C code.");
    } else {
        lua_error(L);
    }
}

bool PCSX::Lua::isobject(int i) {
    bool r = false;
    if (istable(i)) {
        push("__obj");
        gettable(i);
        r = isuserdata();
        pop();
    } else {
        r = isnil(i);
    }
    return r;
}

std::string PCSX::Lua::tostring(int i) {
    switch (type(i)) {
        case LUA_TNIL:
            return "(nil)";
        case LUA_TBOOLEAN:
            return toboolean(i) ? "true" : "false";
        case LUA_TNUMBER:
            return std::to_string(tonumber(i));
        default: {
            size_t l;
            const char* const r = lua_tolstring(L, i, &l);
            return std::string(r, l);
        }
    }
    return "<lua-NULL>";
}

std::string PCSX::Lua::escapeString(const std::string& s) {
    std::string r = "";

    for (int i = 0; i < s.size(); i++) {
        switch (s[i]) {
            case '"':
            case '\\':
                r += '\\';
                r += s[i];
                break;
            case '\n':
                r += "\\n";
                break;
            case '\r':
                r += "\\r";
                break;
            case '\0':
                r += "\\000";
                break;
            default:
                r += s[i];
        }
    }
    return r;
}

void PCSX::Lua::load(const std::string& str, const std::string& name, bool docall) {
    int status = luaL_loadbuffer(L, str.c_str(), str.size(), name.c_str());

    if (status) {
        pushLuaContext();
        displayStack(true);
        while (gettop()) pop();
        throw std::runtime_error("Error loading lua string");
    }

    if (docall) call();
}

void PCSX::Lua::displayStack(bool error) {
    int n = lua_gettop(L);

    getfield("IN_DISPLAY_STACK", LUA_REGISTRYINDEX);
    bool isInDisplayStackAlready = toboolean();
    pop();

    if (n == 0) {
        if ((normalPrinter && !error) || (errorPrinter && error)) {
            if (error) {
                errorPrinter("Stack empty");
            } else {
                normalPrinter("Stack empty");
            }
        }

        if (isInDisplayStackAlready) {
            printf("Stack empty");
            return;
        }

        checkstack(2);
        push(true);
        setfield("IN_DISPLAY_STACK", LUA_REGISTRYINDEX);
        lua_pushstring(L, error ? "printError" : "print");
        lua_gettable(L, LUA_GLOBALSINDEX);
        push("Stack empty");
        pcall(1);
        push(false);
        setfield("IN_DISPLAY_STACK", LUA_REGISTRYINDEX);
        return;
    }

    checkstack(6);
    bool useLuaPrinter = false;

    if ((!normalPrinter && error) || (!errorPrinter && !error)) {
        useLuaPrinter = true;
        lua_pushstring(L, error ? "printError" : "print");
        lua_gettable(L, LUA_GLOBALSINDEX);
    }
    for (int i = 1; i <= n; i++) {
        int c = 3;
        if (useLuaPrinter) {
            lua_pushvalue(L, -1);
        }
        push((lua_Number)i);
        push(": ");
        switch (lua_type(L, i)) {
            case LUA_TNONE:
                push("Invalid");
                break;
            case LUA_TNIL:
                push("(Nil)");
                break;
            case LUA_TNUMBER:
                push("(Number) ");
                copy(i);
                c++;
                break;
            case LUA_TBOOLEAN:
                push("(Bool)   " + std::string(lua_toboolean(L, i) ? "true" : "false"));
                break;
            case LUA_TSTRING:
                push("(String) ");
                copy(i);
                c++;
                break;
            case LUA_TTABLE:
                push("(Table)");
                break;
            case LUA_TFUNCTION:
                push("(Function)");
                break;
            case LUA_TUSERDATA:
                push("(Userdata)");
                break;
            case LUA_TLIGHTUSERDATA:
                push("(Lightuserdata)");
                break;
            case LUA_TTHREAD:
                push("(Thread)");
                break;
            default:
                push("Unknown");
                break;
        }
        concat(c);
        if (useLuaPrinter) {
            if (isInDisplayStackAlready) {
                std::string msg = tostring();
                pop();
                printf("%s\n", msg.c_str());
            } else {
                push(true);
                setfield("IN_DISPLAY_STACK", LUA_REGISTRYINDEX);
                pcall(1);
                push(false);
                setfield("IN_DISPLAY_STACK", LUA_REGISTRYINDEX);
            }
        } else {
            std::string msg = tostring();
            pop();
            if (error) {
                errorPrinter(msg);
            } else {
                normalPrinter(msg);
            }
        }
    }
    pop();
}

json PCSX::Lua::toJson(int t) {
    if (!istable(t)) return {};
    if (t < 0) t = gettop() + t + 1;
    push();
    json ret = {};
    while (next(t) != 0) {
        auto keytype = type(-2);
        auto valtype = type(-1);
        bool keyvalid = false;
        bool valvalid = false;
        std::string key;
        json val;
        switch (keytype) {
            case LUA_TSTRING:
                keyvalid = true;
                key = tostring(-2);
                break;
        }
        switch (valtype) {
            case LUA_TNUMBER: {
                valvalid = true;
                auto num = tonumber(-1);
                double fractpart, intpart;
                fractpart = modf(num, &intpart);
                if (fractpart == 0.0) {
                    val = static_cast<int>(intpart);
                } else {
                    val = num;
                }
            } break;
            case LUA_TBOOLEAN:
                valvalid = true;
                val = toboolean(-1);
                break;
            case LUA_TSTRING:
                valvalid = true;
                val = tostring(-1);
                break;
            case LUA_TTABLE:
                valvalid = true;
                val = toJson(-1);
                break;
        }
        pop();
        if (keyvalid && valvalid) {
            ret[key] = val;
        }
    }

    return ret;
}

void PCSX::Lua::fromJson(const json& j, int t) {
    if (!istable(t)) return;
    if (t < 0) t = gettop() + t + 1;
    if (!j.is_object()) return;

    for (auto it = j.begin(); it != j.end(); it++) {
        switch (it.value().type()) {
            case json::value_t::number_integer:
                push(it.key());
                push(lua_Number(it.value().get<int>()));
                settable(t);
                break;
            case json::value_t::number_float:
                push(it.key());
                push(it.value().get<float>());
                settable(t);
                break;
            case json::value_t::boolean:
                push(it.key());
                push(it.value().get<bool>());
                settable(t);
                break;
            case json::value_t::string:
                push(it.key());
                push(it.value().get<std::string>());
                settable(t);
                break;
            case json::value_t::object:
                push(it.key());
                newtable();
                fromJson(it.value(), -1);
                settable(t);
                break;
        }
    }
}
