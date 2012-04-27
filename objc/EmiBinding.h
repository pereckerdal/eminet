//
//  EmiBinding.h
//  roshambo
//
//  Created by Per Eckerdal on 2012-04-24.
//  Copyright (c) 2012 __MyCompanyName__. All rights reserved.
//

#ifndef roshambo_EmiBinding_h
#define roshambo_EmiBinding_h

#include "EmiTypes.h"
#import <Foundation/Foundation.h>
#include <utility>
#include <ifaddrs.h>

class EmiAddressCmp;
@class GCDAsyncUdpSocket;

class EmiBinding {
private:
    inline EmiBinding();
public:
    
    class Timer;
    typedef void (TimerCb)(EmiTimeInterval now, Timer *timer, void *data);
    class Timer {
    private:
        // Private copy constructor and assignment operator
        inline Timer(const Timer& other);
        inline Timer& operator=(const Timer& other);
    public:
        Timer() : data(NULL), timer(nil), timerCb(NULL) {}
        virtual ~Timer() {
            [timer invalidate];
            timer = nil;
        }
        
        void *data;
        NSTimer *timer;
        TimerCb *timerCb;
    };
    
    typedef __strong NSError* Error;
    typedef EmiAddressCmp AddressCmp;
    typedef GCDAsyncUdpSocket SocketHandle;
    typedef NSData* Address;
    // PersistentData is data that is assumed to be
    // stored until it is explicitly released with the
    // releasePersistentData method. PersistentData must
    // have indefinite extent; it must not be deallocated
    // until releasePersistentData is called on it.
    //
    // PersistentData objects must be copyable and
    // assignable, and these operations must not interfere
    // with the lifetime of the buffer.
    typedef NSData* PersistentData;
    // TemporaryData is data that is assumed to be
    // released after the duration of the call; it can
    // be stored on the stack, for instance. EmiNet core
    // will not explicitly release TempraryData objects.
    //
    // TemporaryData objects must be copyable and
    // assignable, and these operations must not interfere
    // with the lifetime of the buffer.
    typedef NSData* TemporaryData;
    
    inline static NSError *makeError(const char *domain, int32_t code) {
        return [NSError errorWithDomain:[NSString stringWithCString:domain encoding:NSUTF8StringEncoding]
                                   code:code
                               userInfo:nil];
    }
    
    inline static NSData *makePersistentData(const uint8_t *data, size_t length) {
        // The Objective-C EmiNet bindings don't make any distinction
        // between temporary and persistent buffers, because all buffers
        // are persistent.
        return [NSData dataWithBytes:data length:length];
    }
    inline static void releasePersistentData(NSData *data) {
        // Because of ARC, we can leave this as a no-op
    }
    inline static NSData *castToTemporary(NSData *data) {
        return data;
    }
    
    inline static const uint8_t *extractData(NSData *data) {
        return (const uint8_t *)[data bytes];
    }
    inline static size_t extractLength(NSData *data) {
        return [data length];
    }
    
    static const size_t HMAC_HASH_SIZE = 32;
    static void hmacHash(const uint8_t *key, size_t keyLength,
                         const uint8_t *data, size_t dataLength,
                         uint8_t *buf, size_t bufLen);
    static void randomBytes(uint8_t *buf, size_t bufSize);
    
    static Timer *makeTimer();
    static void freeTimer(Timer *timer);
    static void scheduleTimer(Timer *timer, TimerCb *timerCb, void *data, EmiTimeInterval interval, bool repeating);
    static void descheduleTimer(Timer *timer);
    static bool timerIsActive(Timer *timer);
    
    typedef std::pair<ifaddrs*, ifaddrs*> NetworkInterfaces;
    static bool getNetworkInterfaces(NetworkInterfaces& ni, Error& err);
    static bool nextNetworkInterface(NetworkInterfaces& ni, const char*& name, struct sockaddr_storage& addr);
    static void freeNetworkInterfaces(const NetworkInterfaces& ni);
};

#endif
