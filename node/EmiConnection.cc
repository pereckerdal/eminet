#define BUILDING_NODE_EXTENSION

#include "EmiConnection.h"
#include "EmiSocket.h"
#include "EmiNodeUtil.h"
#include "EmiConnectionParams.h"

#include <node.h>

using namespace v8;

Persistent<String>   EmiConnection::channelQualifierSymbol;
Persistent<String>   EmiConnection::prioritySymbol;
Persistent<Function> EmiConnection::constructor;

EmiConnection::EmiConnection(EmiSocket *es, const struct sockaddr_storage& address, uint16_t inboundPort, bool initiator) :
  _conn(EmiConnDelegate(this), inboundPort, address, &es->getSock(), initiator) {};

EmiTimeInterval EmiConnection::Now() {
  return ((double)uv_hrtime())/NSECS_PER_SEC;
}

void EmiConnection::Init(Handle<Object> target) {
  channelQualifierSymbol = Persistent<String>::New(String::NewSymbol("channelQualifier"));
  prioritySymbol = Persistent<String>::New(String::NewSymbol("priority"));
  
  // Prepare constructor template
  Local<FunctionTemplate> tpl = FunctionTemplate::New(New);
  tpl->SetClassName(String::NewSymbol("EmiConnection"));
  tpl->InstanceTemplate()->SetInternalFieldCount(1);
  
  // Prototype
  tpl->PrototypeTemplate()->Set(String::NewSymbol("close"),
                                FunctionTemplate::New(Close)->GetFunction());
  tpl->PrototypeTemplate()->Set(String::NewSymbol("forceClose"),
                                FunctionTemplate::New(ForceClose)->GetFunction());
  tpl->PrototypeTemplate()->Set(String::NewSymbol("closeOrForceClose"),
                                FunctionTemplate::New(CloseOrForceClose)->GetFunction());
  tpl->PrototypeTemplate()->Set(String::NewSymbol("flush"),
                                FunctionTemplate::New(Flush)->GetFunction());
  tpl->PrototypeTemplate()->Set(String::NewSymbol("send"),
                                FunctionTemplate::New(Send)->GetFunction());

  constructor = Persistent<Function>::New(tpl->GetFunction());
}

Handle<Object> EmiConnection::NewInstance(EmiSocket *es, const struct sockaddr_storage& address, uint16_t inboundPort, bool initiator) {
  HandleScope scope;
  
  const unsigned argc = 1;
  Handle<Value> argv[argc] = { EmiConnectionParams::NewInstance(es, address, inboundPort, initiator) };
  Local<Object> instance = constructor->NewInstance(argc, argv);

  return scope.Close(instance);
}

Handle<Value> EmiConnection::New(const Arguments& args) {
  HandleScope scope;
  
  if (1 != args.Length()) {
    ThrowException(Exception::TypeError(String::New("Wrong number of arguments")));
    return scope.Close(Undefined());
  }
  
  EmiConnectionParams *ecp = ObjectWrap::Unwrap<EmiConnectionParams>(args[0]->ToObject());
  EmiConnection *ec = new EmiConnection(ecp->es, ecp->address, ecp->inboundPort, ecp->initiator);
  ec->Wrap(args.This());
  
  return args.This();
}

Handle<Value> EmiConnection::Close(const Arguments& args) {
  HandleScope scope;
  
  if (0 != args.Length()) {
    ThrowException(Exception::TypeError(String::New("Wrong number of arguments")));
    return scope.Close(Undefined());
  }

  EmiConnection* ec = ObjectWrap::Unwrap<EmiConnection>(args.This());
  EmiError err;
  if (!ec->_conn.close(Now(), err)) {
    // TODO Add the information in err to the exception that is thrown
    ThrowException(Exception::Error(String::New("Failed to close connection")));
    return scope.Close(Undefined());
  }

  return scope.Close(Undefined());
}

Handle<Value> EmiConnection::ForceClose(const Arguments& args) {
  HandleScope scope;
  
  if (0 != args.Length()) {
    ThrowException(Exception::TypeError(String::New("Wrong number of arguments")));
    return scope.Close(Undefined());
  }

  EmiConnection* ec = ObjectWrap::Unwrap<EmiConnection>(args.This());
  ec->_conn.forceClose();

  return scope.Close(Undefined());
}

Handle<Value> EmiConnection::CloseOrForceClose(const Arguments& args) {
  HandleScope scope;
  
  if (0 != args.Length()) {
    ThrowException(Exception::TypeError(String::New("Wrong number of arguments")));
    return scope.Close(Undefined());
  }

  EmiConnection* ec = ObjectWrap::Unwrap<EmiConnection>(args.This());
  EmiError err;
  if (!ec->_conn.close(Now(), err)) {
    ec->_conn.forceClose();
  }

  return scope.Close(Undefined());
}

Handle<Value> EmiConnection::Flush(const Arguments& args) {
  HandleScope scope;
  
  if (0 != args.Length()) {
    ThrowException(Exception::TypeError(String::New("Wrong number of arguments")));
    return scope.Close(Undefined());
  }

  EmiConnection* ec = ObjectWrap::Unwrap<EmiConnection>(args.This());
  ec->_conn.flush(Now());

  return scope.Close(Undefined());
}

Handle<Value> EmiConnection::Send(const Arguments& args) {
  HandleScope scope;
  
  
  /// Basic argument checks
  
  size_t numArgs = args.Length();
  if (!(1 == numArgs || 2 == numArgs)) {
    ThrowException(Exception::TypeError(String::New("Wrong number of arguments")));
    return scope.Close(Undefined());
  }
  
  if (!args[0]->IsObject() || (2 == numArgs && !args[1]->IsObject())) {
    ThrowException(Exception::TypeError(String::New("Wrong arguments")));
    return scope.Close(Undefined());
  }
  
  
  /// Extract arguments
  
  EmiChannelQualifier channelQualifier = EMI_CHANNEL_QUALIFIER_DEFAULT;
  EmiPriority priority = EMI_PRIORITY_DEFAULT;
  
  if (2 == numArgs) {
    Local<Object> opts(args[1]->ToObject());
    Local<Value>   cqv(opts->Get(channelQualifierSymbol));
    Local<Value>    pv(opts->Get(prioritySymbol));
    
    if (!cqv.IsEmpty()) {
      if (!cqv->IsNumber()) {
        ThrowException(Exception::TypeError(String::New("Wrong arguments")));
        return scope.Close(Undefined());
      }
      
      channelQualifier = (EmiChannelQualifier) cqv->NumberValue();
    }
    
    if (!pv.IsEmpty()) {
      if (!pv->IsNumber()) {
        ThrowException(Exception::TypeError(String::New("Wrong arguments")));
        return scope.Close(Undefined());
      }
      
      priority = (EmiPriority) pv->NumberValue();
    }
  }
  
  
  // Do the actual send
  
  EmiConnection* ec = ObjectWrap::Unwrap<EmiConnection>(args.This());
  EmiError err;
  if (!ec->_conn.send(Now(), Persistent<Object>::New(args[0]->ToObject()), channelQualifier, priority, err)) {
    // TODO Add the information in err to the exception that is thrown
    ThrowException(Exception::Error(String::New("Failed to send message")));
    return scope.Close(Undefined());
  }
  
  return scope.Close(Undefined());
}