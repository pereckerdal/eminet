#define BUILDING_NODE_EXTENSION

#include "EmiSocket.h"

#include "EmiNodeUtil.h"
#include "EmiConnection.h"

#include <node.h>
#include <stdlib.h>

using namespace v8;

#define EXPAND_SYMS                                        \
  EXPAND_SYM(mtu);                                         \
  EXPAND_SYM(heartbeatFrequency);                          \
  EXPAND_SYM(tickFrequency);                               \
  EXPAND_SYM(heartbeatsBeforeConnectionWarning);           \
  EXPAND_SYM(connectionTimeout);                           \
  EXPAND_SYM(receiverBufferSize);                          \
  EXPAND_SYM(senderBufferSize);                            \
  EXPAND_SYM(acceptConnections);                           \
  EXPAND_SYM(type);                                        \
  EXPAND_SYM(port);                                        \
  EXPAND_SYM(address);                                     \
  EXPAND_SYM(fabricatedPacketDropRate);

#define EXPAND_SYM(sym) Persistent<String> EmiSocket::sym##Symbol;
EXPAND_SYMS
#undef EXPAND_SYM

Persistent<Function> EmiSocket::gotConnection;
Persistent<Function> EmiSocket::connectionMessage;
Persistent<Function> EmiSocket::connectionLost;
Persistent<Function> EmiSocket::connectionRegained;
Persistent<Function> EmiSocket::connectionDisconnect;
Persistent<Function> EmiSocket::connectionError;

static void parseIp(const char* host,
                    uint16_t port,
                    int family,
                    sockaddr_storage *out) {
    if (AF_INET == family) {
        struct sockaddr_in address4(uv_ip4_addr(host, port));
        memcpy(out, &address4, sizeof(struct sockaddr_in));
    }
    else if (AF_INET6 == family) {
        struct sockaddr_in6 address6(uv_ip6_addr(host, port));
        memcpy(out, &address6, sizeof(struct sockaddr_in));
    }
    else {
        ASSERT(0 && "unexpected address family");
        abort();
    }
}

static void anyIp(uint16_t port,
                  int family,
                  sockaddr_storage *out) {
    if (AF_INET6 == family) {
        struct sockaddr_in6& addr6(*((struct sockaddr_in6 *)out));
        addr6.sin6_len       = sizeof(struct sockaddr_in6);
        addr6.sin6_family    = AF_INET6;
        addr6.sin6_port      = htons(port);
        addr6.sin6_flowinfo  = 0;
        addr6.sin6_addr      = in6addr_any;
        addr6.sin6_scope_id  = 0;
    }
    else if (AF_INET == family) {
		struct sockaddr_in& addr(*((struct sockaddr_in *)out));
		addr.sin_len         = sizeof(struct sockaddr_in);
		addr.sin_family      = AF_INET;
		addr.sin_port        = htons(port);
		addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		memset(&(addr.sin_zero), 0, sizeof(addr.sin_zero));
    }
    else {
        ASSERT(0 && "unexpected address family");
        abort();
    }
}

static bool parseAddressFamily(const char* typeStr, int *family) {
    if (0 == strcmp(typeStr, "udp4")) {
        *family = AF_INET;
        return true;
    }
    else if (0 == strcmp(typeStr, "udp6")) {
        *family = AF_INET6;
        return true;
    }
    else {
        return false;
    }
}

EmiSocket::EmiSocket(const EmiSockConfig<EmiSockDelegate::Address>& sc) :
_sock(sc, EmiSockDelegate(*this)) {}

EmiSocket::~EmiSocket() {}

void EmiSocket::Init(Handle<Object> target) {
    // Load symbols
#define EXPAND_SYM(sym) \
  sym##Symbol = Persistent<String>::New(String::NewSymbol(#sym));
    EXPAND_SYMS
#undef EXPAND_SYM
    
    // Prepare constructor template
    Local<FunctionTemplate> tpl = FunctionTemplate::New(New);
    tpl->SetClassName(String::NewSymbol("EmiSocket"));
    tpl->InstanceTemplate()->SetInternalFieldCount(1);
    
    // Prototype
#define X(sym, name)                                        \
  tpl->PrototypeTemplate()->Set(String::NewSymbol(name),    \
      FunctionTemplate::New(sym)->GetFunction());
    X(Suspend,   "suspend");
    X(Desuspend, "desuspend");
    X(Connect4,  "connect4");
    X(Connect6,  "connect6");
#undef X
    
    Persistent<Function> constructor = Persistent<Function>::New(tpl->GetFunction());
    target->Set(String::NewSymbol("EmiSocket"), constructor);
    
    target->Set(String::NewSymbol("setCallbacks"),
                FunctionTemplate::New(SetCallbacks)->GetFunction());
}

#define THROW_TYPE_ERROR(err)                                 \
  do {                                                        \
    ThrowException(Exception::TypeError(String::New(err)));   \
    return scope.Close(Undefined());                          \
  } while (0)

Handle<Value> EmiSocket::SetCallbacks(const Arguments& args) {
    HandleScope scope;
    
    size_t numArgs = args.Length();
    if (6 != numArgs) {
        THROW_TYPE_ERROR("Wrong number of arguments");
    }
    
    if (!args[0]->IsFunction() ||
        !args[1]->IsFunction() ||
        !args[2]->IsFunction() ||
        !args[3]->IsFunction() ||
        !args[4]->IsFunction() ||
        !args[5]->IsFunction()) {
        THROW_TYPE_ERROR("Wrong arguments");
    }
  
#define X(name, num)                                            \
    if (!name.IsEmpty()) name.Dispose();                        \
    Local<Function> name##Fn(Local<Function>::Cast(args[num])); \
    name = Persistent<Function>::New(name##Fn);
    
    X(gotConnection, 0);
    X(connectionMessage, 1);
    X(connectionLost, 2);
    X(connectionRegained, 3);
    X(connectionDisconnect, 4);
    X(connectionError, 5);
    
#undef X
    
    return scope.Close(Undefined());
}

Handle<Value> EmiSocket::New(const Arguments& args) {
    HandleScope scope;
    
    EmiSockConfig<EmiSockDelegate::Address> sc;
    
    size_t numArgs = args.Length();
    if (0 != numArgs && 1 != numArgs) {
        THROW_TYPE_ERROR("Wrong number of arguments");
    }
    
    Local<Object> conf;
    
    if (numArgs && !args[0].IsEmpty() && !args[0]->IsUndefined()) {
        if (!args[0]->IsObject()) {
            THROW_TYPE_ERROR("Wrong arguments");
        }
        
        conf = args[0]->ToObject();
    }
        
#define EXPAND_SYM(sym)                                \
    Local<Value> sym;                                  \
    if (!conf.IsEmpty()) sym = conf->Get(sym##Symbol)
    EXPAND_SYMS
#undef EXPAND_SYM

#define CHECK_PARAM(sym, pred)                                           \
    do {                                                                 \
        if (sym.IsEmpty() || !sym->pred()) {                             \
            THROW_TYPE_ERROR("Invalid socket configuration parameters"); \
        }                                                                \
    } while (0)
#define HAS_PARAM(sym) (!sym.IsEmpty() && !sym->IsUndefined())
#define X(sym, pred, type, cast)                                 \
    do {                                                         \
        if (HAS_PARAM(sym)) {                                    \
            CHECK_PARAM(sym, pred);                              \
            sc.sym = (type) sym->cast();                         \
        }                                                        \
    } while (0)
    
    X(mtu,                               IsNumber,  size_t,          Uint32Value);
    X(heartbeatFrequency,                IsNumber,  float,           NumberValue);
    X(tickFrequency,                     IsNumber,  float,           NumberValue);
    X(heartbeatsBeforeConnectionWarning, IsNumber,  float,           NumberValue);
    X(connectionTimeout,                 IsNumber,  EmiTimeInterval, NumberValue);
    X(senderBufferSize,                  IsNumber,  size_t,          Uint32Value);
    X(acceptConnections,                 IsBoolean, bool,            BooleanValue);
    X(port,                              IsNumber,  uint16_t,        Uint32Value);
    X(fabricatedPacketDropRate,          IsNumber,  EmiTimeInterval, NumberValue);
    
    int family;
    if (HAS_PARAM(type)) {
        CHECK_PARAM(type, IsString);
        
        String::Utf8Value typeStr(type);
        if (!parseAddressFamily(*typeStr, &family)) {
            ThrowException(Exception::Error(String::New("Unknown address family")));
            return scope.Close(Undefined());
        }
    }
    else {
        family = AF_INET;
    }
    
    if (HAS_PARAM(address)) {
        CHECK_PARAM(address, IsString);
        String::Utf8Value host(address);
        parseIp(*host, sc.port, family, &sc.address);
    }
    else {
        anyIp(sc.port, family, &sc.address);
    }
    
#undef CHECK_PARAM
#undef HAS_PARAM
#undef X
    
    EmiSocket* obj = new EmiSocket(sc);
    // We need to Wrap the object now, or failing to desuspend
    // would result in a memory leak. (We rely on Wrap to deallocate
    // obj when it's no longer used.)
    obj->Wrap(args.This());
    
    EmiError err;
    if (!obj->_sock.desuspend(err)) {
        delete obj;
        
        // TODO Add the information in err to the exception that is thrown
        ThrowException(Exception::Error(String::New("Failed to open socket")));
        return scope.Close(Undefined());
    }
    
    return args.This();
}

#define UNWRAP(name, args) EmiSocket *name(ObjectWrap::Unwrap<EmiSocket>(args.This()))

Handle<Value> EmiSocket::Suspend(const Arguments& args) {
    HandleScope scope;
    
    if (0 != args.Length()) {
        THROW_TYPE_ERROR("Wrong number of arguments");
    }
    
    UNWRAP(es, args);
    es->_sock.suspend();
    
    return scope.Close(Undefined());
}

Handle<Value> EmiSocket::Desuspend(const Arguments& args) {
    HandleScope scope;
    
    if (0 != args.Length()) {
        THROW_TYPE_ERROR("Wrong number of arguments");
    }
    
    UNWRAP(es, args);
    EmiError err;
    if (!es->_sock.desuspend(err)) {
        // TODO Add the information in err to the exception that is thrown
        ThrowException(Exception::Error(String::New("Failed to desuspend socket")));
        return scope.Close(Undefined());
    }
    
    return scope.Close(Undefined());
}

Handle<Value> EmiSocket::DoConnect(const Arguments& args, int family) {
    HandleScope scope;
    
    
    /// Extract arguments
    
    if (3 != args.Length()) {
        THROW_TYPE_ERROR("Wrong number of arguments");
    }
    
    if (!args[0]->IsString() || !args[1]->IsNumber() || !args[2]->IsFunction()) {
        THROW_TYPE_ERROR("Wrong arguments");
    }
    
    String::Utf8Value host(args[0]);
    uint16_t          port(args[1]->Uint32Value());
    Local<Object>     callback(args[2]->ToObject());
    
    
    /// Lookup IP
    
    sockaddr_storage address;
    parseIp(*host, port, family, &address);
    
    
    /// Do the actual connect
    
    UNWRAP(es, args);
    EmiError err;
    Persistent<Object> cookie(Persistent<Object>::New(callback));
    if (!es->_sock.connect(EmiConnection::Now(), address, cookie, err)) {
        // Since the connect operation failed, we need to dispose of the
        // cookie.  (If it succeeds, EmiSockDelegate::connectionOpened
        // will take care of the cookie disposal.)
        cookie.Dispose();
        cookie.Clear();
        
        // TODO Add the information in err to the exception that is thrown
        ThrowException(Exception::Error(String::New("Failed to connect")));
        return scope.Close(Undefined());
    }
    
    return scope.Close(Undefined());
}

Handle<Value> EmiSocket::Connect4(const Arguments& args) {
    return DoConnect(args, AF_INET);
}

Handle<Value> EmiSocket::Connect6(const Arguments& args) {
    return DoConnect(args, AF_INET6);
}
