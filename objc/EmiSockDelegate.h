//
//  EmiSockDelegate.h
//  roshambo
//
//  Created by Per Eckerdal on 2012-04-12.
//  Copyright (c) 2012 __MyCompanyName__. All rights reserved.
//

#ifndef emilir_EmiSockDelegate_h
#define emilir_EmiSockDelegate_h

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

class EmiSockDelegate {
    typedef EmiSock<EmiSockDelegate, EmiConnDelegate> ES;
    typedef EmiConn<EmiSockDelegate, EmiConnDelegate> EC;
    
    EmiSocket *_socket;
public:
    
    typedef __strong NSError* Error;
    typedef EmiAddressCmp AddressCmp;
    typedef GCDAsyncUdpSocket SocketHandle;
    typedef NSData* Address;
    typedef NSData* Data;
    typedef void (^__strong ConnectionOpenedCallbackCookie)(NSError *err, EmiConnection *connection);
    
    EmiSockDelegate(EmiSocket *socket);
    
    static void closeSocket(ES& sock, GCDAsyncUdpSocket *socket);
    GCDAsyncUdpSocket *openSocket(ES& sock, uint16_t port, Error& err);
    static uint16_t extractLocalPort(GCDAsyncUdpSocket *socket);
    
    EC *makeConnection(NSData *address, uint16_t inboundPort, bool initiator);
    
    void sendData(GCDAsyncUdpSocket *socket, NSData *address, const uint8_t *data, size_t size);
    void gotConnection(EC& conn);
    
    static void connectionOpened(ConnectionOpenedCallbackCookie& cookie, bool error, EmiDisconnectReason reason, EC& ec);
    
    inline static void panic() {
        [NSException raise:@"EmiNetPanic" format:@"EmiNet internal error"];
    }
    
    inline static NSError *makeError(const char *domain, int32_t code) {
        return [NSError errorWithDomain:[NSString stringWithCString:domain encoding:NSUTF8StringEncoding]
                                   code:code
                               userInfo:nil];
    }
    
    inline static void releaseData(NSData *data) {
        // Because of ARC, we can leave this as a no-op
    }
    
    inline static const uint8_t *extractData(NSData *data) {
        return (const uint8_t *)[data bytes];
    }
    inline static size_t extractLength(NSData *data) {
        return [data length];
    }
};

#endif
