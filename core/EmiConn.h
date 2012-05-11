//
//  EmiConn.h
//  roshambo
//
//  Created by Per Eckerdal on 2012-04-10.
//  Copyright (c) 2012 __MyCompanyName__. All rights reserved.
//

#ifndef emilir_EmiConn_h
#define emilir_EmiConn_h

#include "EmiSock.h"
#include "EmiTypes.h"
#include "EmiSenderBuffer.h"
#include "EmiReceiverBuffer.h"
#include "EmiSendQueue.h"
#include "EmiLogicalConnection.h"
#include "EmiMessage.h"
#include "EmiConnTime.h"
#include "EmiP2PData.h"
#include "EmiConnTimers.h"
#include "EmiUdpSocket.h"

template<class SockDelegate, class ConnDelegate>
class EmiConn {
    typedef typename SockDelegate::Binding   Binding;
    typedef typename Binding::Error          Error;
    typedef typename Binding::PersistentData PersistentData;
    typedef typename Binding::TemporaryData  TemporaryData;
    
    typedef typename SockDelegate::ConnectionOpenedCallbackCookie ConnectionOpenedCallbackCookie;
    
    typedef EmiSock<SockDelegate, ConnDelegate>      ES;
    typedef EmiUdpSocket<SockDelegate>               EUS;
    typedef EmiMessage<Binding>                      EM;
    typedef EmiReceiverBuffer<SockDelegate, EmiConn> ERB;
    typedef EmiSendQueue<SockDelegate, ConnDelegate> ESQ;
    typedef EmiConnTimers<Binding, EmiConn>          ECT;
    typedef EmiLogicalConnection<SockDelegate, ConnDelegate, ERB> ELC;
    
    const uint16_t         _inboundPort;
    // This gets set when we receive the first packet from the other host.
    // Before that, it is an address with port 0
    sockaddr_storage       _localAddress;
    const sockaddr_storage _remoteAddress;
    
    ES&  _emisock;
    EUS *_socket;
    
    EmiP2PData        _p2p;
    EmiConnectionType _type;
    
    ELC *_conn;
    EmiSenderBuffer<Binding> _senderBuffer;
    ERB _receiverBuffer;
    ESQ _sendQueue;
    EmiConnTime _time;
    
    ECT _timers;
    typename Binding::Timer *_forceCloseTimer;
    
    ConnDelegate _delegate;
        
private:
    // Private copy constructor and assignment operator
    inline EmiConn(const EmiConn& other);
    inline EmiConn& operator=(const EmiConn& other);
    
    void deleteELC(ELC *elc) {
        // This if ensures that ConnDelegate::invalidate is only invoked once.
        if (elc) {
            _delegate.invalidate();
            delete elc;
        }
    }
    
    bool enqueueCloseMessageIfEmptySenderBuffer(EmiTimeInterval now, Error& err) {
        if (!_senderBuffer.empty()) {
            // We did not fail, so return true
            return true;
        }
        
        return _conn->enqueueCloseMessage(now, err);
    }
    
    static void forceCloseTimeoutCallback(EmiTimeInterval now, typename Binding::Timer *timer, void *data) {
        EmiConn *conn = (EmiConn *)data;
        Binding::freeTimer(timer);
        conn->forceClose(EMI_REASON_THIS_HOST_CLOSED);
    }
    
public:
    
    // Invoked by EmiReceiverBuffer
    inline void gotReceiverBufferMessage(EmiTimeInterval now, typename ERB::Entry *entry) {
        if (!_conn) return;
        
        // gotMessage should only return false if the message arrived out of order or
        // some other similar error occured, but that should not happen because this
        // callback should only be called by the receiver buffer for messages that are
        // exactly in order.
        ASSERT(_conn->gotMessage(now, entry->header,
                                 Binding::castToTemporary(entry->data), /*offset:*/0,
                                 /*dontFlush:*/true));
    }
    
    EmiConn(const ConnDelegate& delegate, ES& emisock, const EmiConnParams<SockDelegate>& params) :
    _inboundPort(params.inboundPort),
    _remoteAddress(params.address),
    _conn(NULL),
    _delegate(delegate),
    _emisock(emisock),
    _socket(params.socket),
    _type(params.type),
    _p2p(params.p2p),
    _senderBuffer(_emisock.config.senderBufferSize),
    _receiverBuffer(_emisock.config.receiverBufferSize, *this),
    _sendQueue(*this),
    _time(),
    _timers(*this, _time),
    _forceCloseTimer(NULL) {
        EmiNetUtil::anyAddr(0, AF_INET, &_localAddress);
    }
    
    virtual ~EmiConn() {
        _emisock.deregisterConnection(this);
        
        if (_forceCloseTimer) {
            Binding::freeTimer(_forceCloseTimer);
        }
        
        deleteELC(_conn);
    }
    
    // Note that this method might deallocate the connection
    // object! It must not be called from within code that
    // subsequently uses the object.
    void forceClose(EmiDisconnectReason reason) {
        _emisock.deregisterConnection(this);
        
        if (_conn) {
            ELC *conn = _conn;
            
            // _conn is NULLed out to ensure that we don't fire
            // several disconnect events, which would happen if
            // the disconnect delegate callback calls forceClose.
            _conn = NULL;
            
            conn->wasClosed(reason);
            
            // Because we just NULLed out _conn, we need to delete
            // it.
            deleteELC(conn);
        }
    }
    
    void gotPacket() {
        _timers.resetConnectionTimeout();
    }
    
    void gotTimestamp(EmiTimeInterval now, const uint8_t *data, size_t len) {
        _time.gotTimestamp(_emisock.config.heartbeatFrequency, now, data, len);
    }
    
    // Delegates to EmiSendQueue
    void enqueueAck(EmiChannelQualifier channelQualifier, EmiSequenceNumber sequenceNumber) {
        if (_sendQueue.enqueueAck(channelQualifier, sequenceNumber)) {
            _timers.ensureTickTimeout();
        }
    }
    
    // Delegates to EmiSenderBuffer
    void deregisterReliableMessages(EmiTimeInterval now, int32_t channelQualifier, EmiSequenceNumber sequenceNumber) {
        _senderBuffer.deregisterReliableMessages(channelQualifier, sequenceNumber);
        
        // This will clear the rto timeout if the sender buffer is empty
        _timers.updateRtoTimeout();
        
        if (_conn->isClosing()) {
            Error err;
            if (!enqueueCloseMessageIfEmptySenderBuffer(now, err)) {
                // We failed to enqueue the close connection message.
                // I can't think of any reasonable thing to do here but
                // to force close.
                forceClose();
            }
        }
    }
    
    // Returns false if the sender buffer didn't have space for the message.
    // Failing only happens for reliable mesages.
    bool enqueueMessage(EmiTimeInterval now, EmiMessage<Binding> *msg, bool reliable, Error& err) {
        if (reliable) {
            if (!_senderBuffer.registerReliableMessage(msg, err, now)) {
                return false;
            }
            _timers.updateRtoTimeout();
        }
        
        _sendQueue.enqueueMessage(msg, _time, now);
        _timers.ensureTickTimeout();
        
        return true;
    }
    
    // The first time this methods is called, it opens the EmiConnection and returns true.
    // Subsequent times it just resends the init message and returns false.
    bool opened(EmiTimeInterval now, EmiSequenceNumber otherHostInitialSequenceNumber) {
        ASSERT(EMI_CONNECTION_TYPE_SERVER == _type);
        
        if (_conn) {
            // sendInitMessage should not fail, because it can only
            // fail when it attempts to send a SYN message, but we're
            // sending a SYN-RST message here.
            Error err;
            ASSERT(_conn->sendInitMessage(now, err));
            
            return false;
        }
        else {
            _conn = new ELC(this, _receiverBuffer, now, otherHostInitialSequenceNumber);
            return true;
        }
    }
    bool open(EmiTimeInterval now, const ConnectionOpenedCallbackCookie& cookie) {
        ASSERT(EMI_CONNECTION_TYPE_CLIENT == _type);
        
        if (_conn) {
            // We don't need to explicitly resend the init message here;
            // SYN connection init messages like this are reliable messages
            // and will be resent automatically when appropriate.
            return false;
        }
        else {
            _conn = new ELC(this, _receiverBuffer, now, cookie);
            return true;
        }
    }
    
    inline void resetHeartbeatTimeout() {
        _timers.resetHeartbeatTimeout();
    }
    
    // Methods that EmiConnTimers invoke
    
    // Warning: This method might deallocate the object
    inline void connectionTimeout() {
        forceClose(EMI_REASON_CONNECTION_TIMED_OUT);
    }
    inline void connectionLost() {
        _delegate.emiConnLost();
    }
    inline void connectionRegained() {
        _delegate.emiConnLost();
    }
    void rtoTimeout(EmiTimeInterval now, EmiTimeInterval rtoWhenRtoTimerWasScheduled) {
        _senderBuffer.eachCurrentMessage(now, rtoWhenRtoTimerWasScheduled, ^(EmiMessage<Binding> *msg) {
            Error err;
            // Reliable is set to false, because if the message is reliable, it is
            // already in the sender buffer and shouldn't be reinserted anyway
            
            // enqueueMessage can't fail because the reliable parameter is false
            ASSERT(enqueueMessage(now, msg, /*reliable:*/false, err));
        });
    }
    inline void enqueueHeartbeat() {
        _sendQueue.enqueueHeartbeat();
    }
    inline bool senderBufferIsEmpty() const {
        return _senderBuffer.empty();
    }
    
    // Methods that delegate to EmiLogicalConnetion
#define X(msg)                  \
    void got##msg() {           \
        if (_conn) {            \
            _conn->got##msg();  \
        }                       \
    }
    // Warning: This method might deallocate the object
    X(Rst);
    // Warning: This method might deallocate the object
    X(SynRstAck);
    X(PrxRstAck);
#undef X
    void gotPrx(EmiTimeInterval now) {
        if (_conn) {
            _conn->gotPrx(now);
        }
    }
    void gotPrxRstSynAck(EmiTimeInterval now, const uint8_t *data, size_t len) {
        if (_conn) {
            _conn->gotPrxRstSynAck(now, data, len);
        }
    }
    // Delegates to EmiLogicalConnection
    bool gotSynRst(EmiTimeInterval now,
                   const sockaddr_storage& inboundAddr,
                   EmiSequenceNumber otherHostInitialSequenceNumber) {
        _localAddress = inboundAddr;
        return _conn && _conn->gotSynRst(now, inboundAddr, otherHostInitialSequenceNumber);
    }
    // Delegates to EmiLogicalConnection
    bool gotMessage(EmiTimeInterval now,
                    const EmiMessageHeader& header,
                    const TemporaryData& data, size_t offset,
                    bool dontFlush) {
        if (!_conn) {
            return false;
        }
        else {
            return _conn->gotMessage(now, header, data, offset, dontFlush);
        }
    }
    
    
    // Invoked by EmiLogicalConnection
    void emitDisconnect(EmiDisconnectReason reason) {
        _delegate.emiConnDisconnect(reason);
    }
    void emitMessage(EmiChannelQualifier channelQualifier, const TemporaryData& data, size_t offset, size_t size) {
        _delegate.emiConnMessage(channelQualifier, data, offset, size);
    }
    
    bool close(EmiTimeInterval now, Error& err) {
        if (_conn) {
            if (!_conn->initiateCloseProcess(now, err)) {
                return false;
            }
            
            if (!enqueueCloseMessageIfEmptySenderBuffer(now, err)) {
                return false;
            }
            
            return true;
        }
        else {
            // We're already closed
            err = Binding::makeError("com.emilir.eminet.closed", 0);
            return false;
        }
    }
    // Immediately closes the connection without notifying the other host.
    //
    // Note that if this method is used, there is no guarantee that you can
    // reconnect to the remote host immediately; this host immediately forgets
    // that it has been connected, but the other host does not. When
    // reconnecting, this host's OS might pick the same inbound port, and that
    // will confuse the remote host so the connection won't be established.
    void forceClose() {
        // To ensure that we don't deallocate this object immediately (which
        // could have happened if we invoked forceClose(EmiDisconnectReason)
        // immediately, we schedule a timer that will invoke forceClose later
        // on. This guarantees that we don't deallocate this object while
        // there are references to it left on the stack.
        if (!_forceCloseTimer) {
            _forceCloseTimer = Binding::makeTimer();
            Binding::scheduleTimer(_forceCloseTimer, forceCloseTimeoutCallback,
                                   this, /*time:*/0, /*repeating:*/false);
        }
    }
    
    // Delegates to EmiSendQueue
    bool flush(EmiTimeInterval now) {
        return _sendQueue.flush(_time, now);
    }
    
    // Delegates to EmiLogicalConnection
    //
    // This method assumes ownership over the data parameter, and will release it
    // with SockDelegate::releaseData when it's done with it. The buffer must not
    // be modified or released until after SockDelegate::releasePersistentData has
    // been called on it.
    bool send(EmiTimeInterval now, const PersistentData& data, EmiChannelQualifier channelQualifier, EmiPriority priority, Error& err) {
        if (!_conn || _conn->isClosing()) {
            err = Binding::makeError("com.emilir.eminet.closed", 0);
            Binding::releasePersistentData(data);
            return false;
        }
        else {
            return _conn->send(data, now, channelQualifier, priority, err);
        }
    }
    
    inline ConnDelegate& getDelegate() {
        return _delegate;
    }
    
    inline const ConnDelegate& getDelegate() const {
        return _delegate;
    }
    inline void setDelegate(const ConnDelegate& delegate) {
        _delegate = delegate;
    }
    
    inline uint16_t getInboundPort() const {
        return _inboundPort;
    }
    
    inline const sockaddr_storage& getLocalAddress() const {
        return _localAddress;
    }
    
    inline const sockaddr_storage& getRemoteAddress() const {
        return _remoteAddress;
    }
    
    inline bool issuedConnectionWarning() const {
        return _timers.issuedConnectionWarning();
    }
    inline const EUS *getSocket() const {
        return _socket;
    }
    inline EUS *getSocket() {
        return _socket;
    }
    inline EmiConnectionType getType() const {
        return _type;
    }
    bool isOpen() const {
        return _conn && !_conn->isOpening() && !_conn->isClosing();
    }
    bool isOpening() const {
        return _conn && _conn->isOpening();
    }
    EmiSequenceNumber getOtherHostInitialSequenceNumber() const {
        return _conn ? _conn->getOtherHostInitialSequenceNumber() : 0;
    }
    
    // Invoked by EmiSendQueue
    void sendDatagram(const uint8_t *data, size_t size) {
        sendDatagram(getRemoteAddress(), data, size);
    }
    
    // Invoked by EmiLogicalConnection
    void sendDatagram(const sockaddr_storage& address, const uint8_t *data, size_t size) {
        _timers.sentPacket();
        
        if (_emisock.shouldArtificiallyDropPacket()) {
            return;
        }
        
        if (_socket) {
            _socket->sendData(_localAddress, address, data, size);
        }
    }
    
    void closeSocket() {
        if (_socket) {
            delete _socket;
            _socket = NULL;
        }
    }
    
    inline inline ES& getEmiSock() {
        return _emisock;
    }
    
    inline inline const EmiP2PData& getP2PData() const {
        return _p2p;
    }
};

#endif
