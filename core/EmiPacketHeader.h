//
//  EmiPacketHeader.h
//  eminet
//
//  Created by Per Eckerdal on 2012-02-16.
//  Copyright (c) 2012 Per Eckerdal. All rights reserved.
//

#ifndef eminet_EmiPacketHeader_h
#define eminet_EmiPacketHeader_h

#include "EmiTypes.h"

#include <stdint.h>
#include <cstddef>

static const uint32_t EMI_PACKET_HEADER_MAX_RESPONSE_DELAY = 255;

// A message header, as it is represented in the receiver side of things,
// in a computation friendly format (the actual wire format is more
// condensed)
class EmiPacketHeader {
public:
    EmiPacketHeader();
    virtual ~EmiPacketHeader();
    
    EmiPacketFlags flags;
    EmiPacketSequenceNumber sequenceNumber; // Set if (flags & EMI_SEQUENCE_NUMBER_PACKET_FLAG)
    EmiPacketSequenceNumber ack; // Set if (flags & EMI_ACK_PACKET_FLAG)
    EmiPacketSequenceNumber nak; // Set if (flags & EMI_NAK_PACKET_FLAG)
    float linkCapacity; // Set if (flags & EMI_LINK_CAPACITY_PACKET_FLAG)
    float arrivalRate; // Set if (flags & EMI_ARRIVAL_RATE_PACKET_FLAG)
    EmiPacketSequenceNumber rttResponse; // Set if (flags & EMI_RTT_RESPONSE_PACKET_FLAG)
    
    // The maximum value of the RTT response delay is 255 ms.
    // This should be more than enough, since the other host
    // is supposed to send an RTT response within a tick, which
    // is 10 ms.
    uint8_t rttResponseDelay; // Set if (flags & EMI_RTT_RESPONSE_PACKET_FLAG)
    
    // Returns true if the parse was successful
    //
    // Note that this method does not check that the entire
    // packet fits in the buffer, only that the header fits.
    //
    // headerLength will be set to the header length. headerLength
    // can be NULL, in which case it's not set.
    static bool parse(const uint8_t *buf, size_t bufSize, EmiPacketHeader *header, size_t *headerLength);
    
    // Returns true if the write was successful
    // 
    // headerLength will be set to the header length. headerLength
    // can be NULL, in which case it's not set.
    static bool write(uint8_t *buf, size_t bufSize, const EmiPacketHeader& header, size_t *headerLength);
    
    // Writes an empty packet header to buf.
    // 
    // Returns true if the write was successful. This only fails if the
    // buffer was too small.
    // 
    // headerLength will be set to the header length. headerLength
    // can be NULL, in which case it's not set.
    static bool writeEmpty(uint8_t *buf, size_t bufSize, size_t *headerLength);
    
    // Adds fillerSize bytes of filler to the packet.
    //
    // Assumes that the buffer is at least of size packetSize+fillerSize.
    //
    // Assumes that the packet data does not already contain filler data.
    //
    // Assumes that the buffer actually contains packet data, including
    // a valid header.
    static void addFillerBytes(uint8_t *buf, size_t packetSize, uint16_t fillerSize);
};

#endif
