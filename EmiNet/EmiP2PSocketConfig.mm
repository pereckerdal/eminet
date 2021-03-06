//
//  EmiP2PSocketConfig.m
//  eminet
//
//  Created by Per Eckerdal on 2012-04-23.
//  Copyright (c) 2012 Per Eckerdal. All rights reserved.
//

#import "EmiP2PSocketConfig.h"
#import "EmiP2PSocketConfigInternal.h"

typedef EmiP2PSocketConfigSC SC;

@implementation EmiP2PSocketConfig {
    void *_sc;
}

- (id)init {
    if (self = [super init]) {
        _sc = new SC();
    }
    return self;
}

- (void)dealloc {
    if (_sc) {
        delete (SC *)_sc;
        _sc = nil;
    }
}

- (NSData *)serverAddress {
    const sockaddr_storage& ss(((SC *)_sc)->address);
    return [NSData dataWithBytes:&ss length:EmiNetUtil::addrSize(ss)];
}

- (void)setServerAddress:(NSData *)serverAddress {
    sockaddr_storage ss;
    memcpy(&ss, [serverAddress bytes], MIN([serverAddress length], sizeof(sockaddr_storage)));
    ((SC *)_sc)->address = ss;
}

- (EmiTimeInterval)connectionTimeout {
    return ((SC *)_sc)->connectionTimeout;
}

- (void)setConnectionTimeout:(EmiTimeInterval)connectionTimeout {
    ((SC *)_sc)->connectionTimeout = connectionTimeout;
}

- (NSUInteger)rateLimit {
    return ((SC *)_sc)->rateLimit;
}

- (void)setRateLimit:(NSUInteger)rateLimit {
    ((SC *)_sc)->rateLimit = rateLimit;
}

- (uint16_t)serverPort {
    return ((SC *)_sc)->port;
}

- (void)setServerPort:(uint16_t)serverPort {
    ((SC *)_sc)->port = serverPort;
}

- (SC *)sockConfig {
    return (SC *)_sc;
}

@end
