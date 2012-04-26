//
//  EmiSock.h
//  roshambo
//
//  Created by Per Eckerdal on 2012-04-11.
//  Copyright (c) 2012 __MyCompanyName__. All rights reserved.
//

#ifndef emilir_EmiSock_h
#define emilir_EmiSock_h

#include "EmiMessageHeader.h"
#include "EmiSendQueue.h"
#include "EmiSockConfig.h"
#include "EmiConnParams.h"

#include <map>
#include <set>
#include <cstdlib>

static const uint64_t ARC4RANDOM_MAX = 0x100000000;

template<class SockDelegate, class ConnDelegate>
class EmiSock {
    typedef typename SockDelegate::Binding     Binding;
    typedef typename Binding::Error            Error;
    typedef typename Binding::TemporaryData    TemporaryData;
    typedef typename Binding::Address          Address;
    typedef typename Binding::AddressCmp       AddressCmp;
    typedef typename Binding::SocketHandle     SocketHandle;
    typedef typename SockDelegate::ConnectionOpenedCallbackCookie  ConnectionOpenedCallbackCookie;
    
    class EmiConnectionKey {
        const AddressCmp _cmp;
    public:
        EmiConnectionKey(const Address& address_, uint16_t inboundPort_) :
        address(address_), inboundPort(inboundPort_), _cmp() {}
        EmiConnectionKey(const Address& address_, uint16_t inboundPort_, const AddressCmp& cmp) :
        address(address_), inboundPort(inboundPort_), _cmp(cmp) {}
        
        Address address;
        uint16_t inboundPort;
        
        inline bool operator<(const EmiConnectionKey& rhs) const {
            if (inboundPort < rhs.inboundPort) return true;
            else if (inboundPort > rhs.inboundPort) return false;
            else {
                return 0 > _cmp(address, rhs.address);
            }
        }
    };
    
    struct EmiClientSocketKey {
        const AddressCmp _cmp;
    public:
        EmiClientSocketKey(const Address& address_) :
        address(address_), _cmp() {}
        EmiClientSocketKey(const Address& address_, const AddressCmp& cmp) :
        address(address_), _cmp(cmp) {}
        
        Address address;
        
        inline bool operator<(const EmiClientSocketKey& rhs) const {
            return 0 > _cmp(address, rhs.address);
        }
    };
    
    struct EmiClientSocket {
        EmiClientSocket(EmiSock& emiSock_, uint16_t port_) :
        emiSock(emiSock_), port(port_), socket(NULL) {}
        
        EmiSock& emiSock;
        uint16_t port;
        SocketHandle *socket;
        std::set<EmiClientSocketKey> addresses;
        
        bool open(Error& err) {
            if (!socket) {
                socket = emiSock._delegate.openSocket(emiSock.config.address, port, err);
                if (0 == port) {
                    port = SockDelegate::extractLocalPort(socket);
                }
            }
            
            return !!socket;
        }
        
        void close() {
            SockDelegate::closeSocket(emiSock, socket);
            socket = NULL;
        }
    };
    
    typedef EmiConnParams                       ECP;
    typedef EmiConn<SockDelegate, ConnDelegate> EC;
    typedef EmiMessage<Binding>                 EM;
    
    typedef std::map<EmiConnectionKey, EC*>       EmiConnectionMap;
    typedef typename EmiConnectionMap::iterator   EmiConnectionMapIter;
    typedef std::map<uint16_t, EmiClientSocket>   EmiClientSocketMap;
    typedef typename EmiClientSocketMap::iterator EmiClientSocketMapIter;
    
private:
    // Private copy constructor and assignment operator
    inline EmiSock(const EmiSock& other);
    inline EmiSock& operator=(const EmiSock& other);
    
    SocketHandle         *_serverSocket;
    EmiConnectionMap      _conns;
    EmiClientSocketMap    _clientSockets;
    SockDelegate          _delegate;
    
    int32_t findFreeClientPort(const Address& address) {
        EmiClientSocketKey key(address);
        
        EmiClientSocketMapIter iter = _clientSockets.begin();
        EmiClientSocketMapIter end  = _clientSockets.end();
        while (iter != end) {
            if (0 == (*iter).second.addresses.count(key)) {
                return (*iter).first;
            }
            
            ++iter;
        }
        
        return -1;
    }
    
    uint16_t openClientSocket(const Address& address, Error& err) {
        // Ensure that the datagram sockets are open
        if (!desuspend(err)) {
            return 0;
        }
        
        int32_t inboundPort = findFreeClientPort(address);
        if (-1 == inboundPort) {
            EmiClientSocket ecs(*this, 0);
            
            Error err;
            if (!ecs.open(err)) {
                return 0;
            }
            inboundPort = ecs.port;
            
            _clientSockets.insert(typename EmiClientSocketMap::value_type(inboundPort, ecs));
        }
        
        (*(_clientSockets.find(inboundPort))).second.addresses.insert(EmiClientSocketKey(address));
        
        return inboundPort;
    }
    
    void suspendIfInactive() {
        if (!config.acceptConnections && _clientSockets.empty()) {
            suspend();
        }
    }
    
    inline bool shouldArtificiallyDropPacket() const {
        if (0 == config.fabricatedPacketDropRate) return false;
        
        return ((float)arc4random() / ARC4RANDOM_MAX) < config.fabricatedPacketDropRate;
    }
    
    // SockDelegate::connectionOpened will be called on the cookie iff this function returns true.
    bool connectHelper(EmiTimeInterval now, const Address& address,
                       const uint8_t *p2pCookie, size_t p2pCookieLength,
                       const uint8_t *sharedSecret, size_t sharedSecretLength,
                       const ConnectionOpenedCallbackCookie& callbackCookie, Error& err) {
        uint16_t inboundPort = openClientSocket(address, err);
        
        if (!inboundPort) {
            return false;
        }
        
        EmiConnectionKey key(address, inboundPort);
        // Assert that openClientSocket returned an unused port number.
        ASSERT(0 == _conns.count(key));
        
        EC *ec(_delegate.makeConnection(ECP(address, inboundPort, 
                                            p2pCookie, p2pCookieLength,
                                            sharedSecret, sharedSecretLength)));
        _conns.insert(std::make_pair(key, ec));
        ec->open(now, callbackCookie);
        
        return true;
    }

    
public:
    const EmiSockConfig<Address>  config;
    
    EmiSock(const EmiSockConfig<Address>& config_, const SockDelegate& delegate) :
    config(config_), _delegate(delegate), _serverSocket(NULL) {}
    
    virtual ~EmiSock() {
        // EmiSock should not be deleted before all open connections are closed,
        // but just to be sure, we close all remaining connections.
        size_t numConns = _conns.size();
        EmiConnectionMapIter iter = _conns.begin();
        EmiConnectionMapIter end  = _conns.end();
        while (iter != end) {
            // This will remove the connection from _conns
            (*iter).second->forceClose();
            
            // We do this check to make sure we don't enter an infinite loop.
            // It shouldn't be required.
            size_t newNumConns = _conns.size();
            ASSERT(newNumConns < numConns);
            numConns = newNumConns;
            
            // We can't increment iter, it has been
            // invalidated because the connection was
            // removed from _conns
            iter = _conns.begin();
        }
        
        // This will close (which, depending on the binding, might mean deallocate)
        // all sockets. (By now all sockets except possibly the server should be closed
        // already.)
        suspend();
    }
    
    SockDelegate& getDelegate() {
        return _delegate;
    }
    
    const SockDelegate& getDelegate() const {
        return _delegate;
    }
    
    bool isOpen() const {
        return _serverSocket || !_clientSockets.empty();
    }
    
    void suspend() {
        if (isOpen()) {
            if (_serverSocket) {
                SockDelegate::closeSocket(*this, _serverSocket);
                _serverSocket = NULL;
            }
            
            EmiClientSocketMapIter iter = _clientSockets.begin();
            EmiClientSocketMapIter end = _clientSockets.end();
            while (iter != end) {
                (*iter).second.close();
                ++iter;
            }
        }
    }
    
    bool desuspend(Error& err) {
        if (!isOpen()) {
            if (config.acceptConnections || !_conns.empty()) {
                _serverSocket = _delegate.openSocket(config.address, config.port, err);
                if (!_serverSocket) return false;
            }
            
            EmiClientSocketMapIter iter = _clientSockets.begin();
            EmiClientSocketMapIter end = _clientSockets.end();
            while (iter != end) {
                if (!(*iter).second.open(err)) {
                    return false;
                }
                ++iter;
            }
        }
        
        return true;
    }
    
    void onMessage(EmiTimeInterval now,
                   SocketHandle *sock,
                   const Address& address,
                   const TemporaryData& data,
                   size_t offset,
                   size_t len) {
        if (shouldArtificiallyDropPacket()) {
            return;
        }
        
        __block const char *err = NULL;
        
        uint16_t inboundPort(SockDelegate::extractLocalPort(sock));
        
        const uint8_t *rawData(Binding::extractData(data)+offset);
        
        EmiConnectionKey ckey(address, inboundPort);
        EmiConnectionMapIter cur = _conns.find(ckey);
        __block EC *conn = _conns.end() == cur ? NULL : (*cur).second;
        
        if (conn) {
            conn->gotPacket();
        }
        
        if (EMI_TIMESTAMP_LENGTH+1 == len) {
            // This is a heartbeat packet
            if (conn) {
                conn->gotTimestamp(now, rawData, len);
            }
        }
        else if (len < EMI_TIMESTAMP_LENGTH + EMI_HEADER_LENGTH) {
            err = "Packet too short";
            goto error;
        }
        else {
            EmiMessageHeader::EmiParseMessageBlock block =
            ^ bool (const EmiMessageHeader& header, size_t dataOffset) {
                size_t actualDataOffset = dataOffset+EMI_TIMESTAMP_LENGTH+offset;
                
#define ENSURE_CONN(msg)                                            \
                do {                                                \
                    if (!conn) {                                    \
                        err = "Got "msg" message but has no "       \
                              "open connection for that address";   \
                        return false;                               \
                    }                                               \
                } while (0)
#define ENSURE(check, errStr)                   \
                do {                            \
                    if (!(check)) {             \
                        err = errStr;           \
                        return false;           \
                    }                           \
                } while (0)
                
                bool prxFlag  = header.flags & EMI_PRX_FLAG;
                bool synFlag  = header.flags & EMI_SYN_FLAG;
                bool rstFlag  = header.flags & EMI_RST_FLAG;
                bool ackFlag  = header.flags & EMI_ACK_FLAG;
                bool sackFlag = header.flags & EMI_SACK_FLAG;
                
                if (prxFlag) {
                    // This is some kind of proxy/P2P connection message
                    
                    if (!synFlag && !rstFlag && !ackFlag) {
                        ENSURE_CONN("PRX");
                        
                        conn->gotPrx();
                    }
                    if (synFlag && rstFlag && ackFlag) {
                        ENSURE_CONN("PRX-RST-SYN-ACK");
                        
                        // TODO conn->gotPrxRstSynAck();
                    }
                    if (!synFlag && rstFlag && ackFlag) {
                        ENSURE_CONN("PRX-RST-ACK");
                        
                        conn->gotPrxRstAck();
                    }
                    if (synFlag && !rstFlag && !ackFlag) {
                        ENSURE_CONN("PRX-SYN");
                        
                        // TODO conn->gotPrxSyn();
                    }
                    if (synFlag && !rstFlag && ackFlag) {
                        ENSURE_CONN("PRX-SYN-ACK");
                        
                        // TODO conn->gotPrxSynAck();
                    }
                    else {
                        err = "Invalid message flags";
                        return false;
                    }
                }
                else if (synFlag && !rstFlag) {
                    // This is an initiate connection message
                    
                    ENSURE(config.acceptConnections,
                           "Got SYN but this socket doesn't \
                           accept incoming connections");
                    ENSURE(0 == header.length,
                           "Got SYN message with message length != 0");
                    ENSURE(!ackFlag, "Got SYN message with ACK flag");
                    ENSURE(!sackFlag, "Got SYN message with SACK flag");
                    
                    if (conn && conn->isOpen() && conn->getOtherHostInitialSequenceNumber() != header.sequenceNumber) {
                        // The connection is already open, and we get a SYN message with a
                        // different initial sequence number. This probably means that the
                        // other host has forgot about the connection we have open. Force
                        // close it and continue as if conn did not exist.
                        conn->forceClose();
                        conn = NULL;
                    }
                    
                    if (!conn) {
                        conn = _delegate.makeConnection(ECP(address, inboundPort, EMI_CONNECTION_TYPE_SERVER));
                        _conns.insert(std::make_pair(ckey, conn));
                    }
                    
                    conn->gotTimestamp(now, rawData, len);
                    
                    if (conn->opened(now, header.sequenceNumber)) {
                        _delegate.gotConnection(*conn);
                    }
                }
                else if (synFlag && rstFlag) {
                    if (ackFlag) {
                        // This is a close connection ack message
                        
                        ENSURE(!sackFlag, "Got SYN-RST-ACK message with SACK flag");
                        ENSURE(conn,
                               "Got SYN-RST-ACK message but has no open \
                                connection for that address. Ignoring the \
                                packet. (This is not really an error \
                                condition, it is part of normal operation \
                                of the protocol.)");
                        
                        // With this packet type, we do not invoke gotTimestamp:
                        // on the connection object, because the timestamps might be bogus
                        // (since the other host might have forgot about the connection
                        // and thus the data required to send proper timestamps)
                        
                        conn->gotSynRstAck();
                    }
                    else {
                        // This is a connection initiated message
                        
                        ENSURE(!sackFlag, "Got SYN-RST message with SACK flag");
                        ENSURE_CONN("SYN-RST");
                        ENSURE(conn->isOpening(), "Got SYN-RST message for open connection");
                        
                        conn->gotTimestamp(now, rawData, len);
                        if (!conn->gotSynRst(header.sequenceNumber)) {
                            err = "Failed to process SYN-RST message";
                            return false;
                        }
                    }
                }
                else if (!synFlag && rstFlag) {
                    // This is a close connection message
                    
                    ENSURE(!ackFlag, "Got RST message with ACK flag");
                    ENSURE(!sackFlag, "Got RST message with SACK flag");
                    
                    if (conn) {
                        conn->gotTimestamp(now, rawData, len);
                        conn->gotRst();
                    }
                    
                    // Regardless of whether we still have a connection up, respond with a SYN-RST-ACK message
                    EM::writeControlPacket(EMI_SYN_FLAG | EMI_RST_FLAG | EMI_ACK_FLAG, ^(uint8_t *buf, size_t size) {
                        _delegate.sendData(sock, address, buf, size);
                    });
                }
                else if (!synFlag && !rstFlag) {
                    // This is a data message
                    ENSURE_CONN("data");
                    
                    conn->gotTimestamp(now, rawData, len);
                    conn->gotMessage(header, data, actualDataOffset, /*dontFlush:*/false);
                }
                else {
                    err = "Invalid message flags";
                    return false;
                }
                
                return true;
            };
            
            if (!EmiMessageHeader::parseMessages(rawData+EMI_TIMESTAMP_LENGTH,
                                                 len-EMI_TIMESTAMP_LENGTH,
                                                 block)) {
                goto error;
            }
        }
        
        return;
    error:
        
        return;
    }
    
    // SockDelegate::connectionOpened will be called on the cookie iff this function returns true.
    bool connect(EmiTimeInterval now, const Address& address, const ConnectionOpenedCallbackCookie& callbackCookie, Error& err) {
        return connectHelper(now, address,
                             /*p2pCookie:*/NULL, /*p2pCookieLength:*/0,
                             /*sharedSecret:*/NULL, /*sharedSecretLength:*/0,
                             callbackCookie, err);
    }
    
    // SockDelegate::connectionOpened will be called on the cookie iff this function returns true.
    bool connectP2P(EmiTimeInterval now, const Address& address,
                    const uint8_t *p2pCookie, size_t p2pCookieLength,
                    const uint8_t *sharedSecret, size_t sharedSecretLength,
                    const ConnectionOpenedCallbackCookie& callbackCookie, Error& err) {
        return connectHelper(now, address,
                             p2pCookie, p2pCookieLength,
                             sharedSecret, sharedSecretLength,
                             callbackCookie, err);
    }
    
    void sendDatagram(EC *conn, const uint8_t *data, size_t size) {
        conn->sentPacket();
        
        if (shouldArtificiallyDropPacket()) {
            return;
        }
        
        SocketHandle *socket = NULL;
        
        if (EMI_CONNECTION_TYPE_SERVER != conn->getType()) {
            EmiClientSocketMapIter cur = _clientSockets.find(conn->getInboundPort());
            socket = _clientSockets.end() == cur ? NULL : (*cur).second.socket;
        }
        else {
            socket = _serverSocket;
        }
        
        // I'm not 100% sure that socket will never be null
        if (socket) {
            _delegate.sendData(socket, conn->getAddress(), data, size);
        }
    }
    
    void deregisterConnection(EC *conn) {
        const Address& address = conn->getAddress();
        uint16_t inboundPort = conn->getInboundPort();
        
        EmiClientSocketMapIter cur = _clientSockets.find(inboundPort);
        if (_clientSockets.end() != cur) {
            EmiClientSocket& cs((*cur).second);
            cs.addresses.erase(EmiClientSocketKey(address));
            
            if (cs.addresses.empty()) {
                cs.close();
                _clientSockets.erase(inboundPort);
            }
        }
        
        _conns.erase(EmiConnectionKey(address, inboundPort));
        
        suspendIfInactive();
    }
};

#endif
