//
//  EmiDispatchQueueWrapper.h
//  eminet
//
//  Created by Per Eckerdal on 2012-06-08.
//  Copyright (c) 2012 Per Eckerdal. All rights reserved.
//

#import <Foundation/Foundation.h>

@interface EmiDispatchQueueWrapper : NSObject {
@public
    dispatch_queue_t queue;
}

- (id)initWithQueue:(dispatch_queue_t)queue;

@end
