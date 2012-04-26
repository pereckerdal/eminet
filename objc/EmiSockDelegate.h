//
//  EmiSockDelegate.h
//  roshambo
//
//  Created by Per Eckerdal on 2012-04-12.
//  Copyright (c) 2012 __MyCompanyName__. All rights reserved.
//

#ifndef emilir_EmiSockDelegate_h
#define emilir_EmiSockDelegate_h

#include "EmiBinding.h"

#include "EmiTypes.h"
#import <Foundation/Foundation.h>

@class EmiSocket;
@class EmiConnection;
@class GCDAsyncUdpSocket;
class EmiSockDelegate;
class EmiConnDelegate;
class EmiAddressCmp;
template<class SockDelegate, class ConnDelegate>
class EmiSock;
template<class SockDelegate, class ConnDelegate>
class EmiConn;
class EmiConnParams;

class EmiSockDelegate {
    typedef EmiSock<EmiSockDelegate, EmiConnDelegate> ES;
    typedef EmiConn<EmiSockDelegate, EmiConnDelegate> EC;
    
    EmiSocket *_socket;
public:
    
    typedef EmiBinding Binding;
    typedef void (^__strong ConnectionOpenedCallbackCookie)(NSError *err, EmiConnection *connection);
    
    EmiSockDelegate(EmiSocket *socket);
    
    static void closeSocket(ES& sock, GCDAsyncUdpSocket *socket);
    GCDAsyncUdpSocket *openSocket(NSData *address, uint16_t port, __strong NSError*& err);
    static uint16_t extractLocalPort(GCDAsyncUdpSocket *socket);
    
    EC *makeConnection(const EmiConnParams& params);
    
    void sendData(GCDAsyncUdpSocket *socket, NSData *address, const uint8_t *data, size_t size);
    void gotConnection(EC& conn);
    
    static void connectionOpened(ConnectionOpenedCallbackCookie& cookie, bool error, EmiDisconnectReason reason, EC& ec);
};

#endif
