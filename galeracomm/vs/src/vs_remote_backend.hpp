#ifndef VS_REMOTE_BACKEND_HPP
#define VS_REMOTE_BACKEND_HPP

#include "gcomm/address.hpp"
#include "gcomm/transport.hpp"

class VSRCommand : public Serializable {
    Address addr;
public:
    enum Type {SET, JOIN, LEAVE, RESULT} type;
    enum Result {SUCCESS, FAIL} result;
    
    VSRCommand() {

    }
    VSRCommand(const Type t) : type(t), result(SUCCESS) {
	
    }
    VSRCommand(const Type t, const Address a) : addr(a), type(t), result(SUCCESS) {
    }

    VSRCommand(const Type t, const Result r) : type(t), result(r) {

    }
    
    Address get_address() const {
	return addr;
    }
    
    Type get_type() const {
	return type;
    }
    Result get_result() const {
	return result;
    }

    size_t read(const void *buf, const size_t buflen, const size_t offset) {
	uint32_t w;
	size_t off;
	if ((off = read_uint32(buf, buflen, offset, &w)) == 0)
	    return 0;
	type = static_cast<Type>(w & 0xff);
	result = static_cast<Result>((w >> 8) & 0xff);
	switch (type) {
	case JOIN:
	case LEAVE:
	    if ((off = addr.read(buf, buflen, off)) == 0)
		return 0;
	    break;
	case SET:
	case RESULT:
	    break;
	}
	return off;
    }
    size_t write(void *buf, const size_t buflen, const size_t offset) const {
	size_t off;
	uint32_t w = type | (result << 8);
	if ((off = write_uint32(w, buf, buflen, offset)) == 0)
	    return 0;
	if ((off = addr.write(buf, buflen, off)) == 0)
	    return 0;
	assert(off == 8 + offset);
	return off;
    }
    size_t size() const {
	return 4 + addr.size();
    }
};

class VSRMessage : public Serializable {
    Address base_addr;
    VSRCommand cmd;
    size_t raw_len;
    unsigned char raw[64];
public:
    enum Type {HANDSHAKE, CONTROL, VSPROTO} type;
    
    VSRMessage() : type(VSPROTO) {
	if ((raw_len = write(raw, sizeof(raw), 0)) == 0)
	    throw DException("");
    }

    VSRMessage(const Address a) : base_addr(a), type(HANDSHAKE) {
	if ((raw_len = write(raw, sizeof(raw), 0)) == 0)
	    throw DException("");
    }
    
    VSRMessage(const VSRCommand& c) : cmd(c) , type(CONTROL) {
	if ((raw_len = write(raw, sizeof(raw), 0)) == 0)
	    throw DException("");
    } 
    
    Type get_type() const {
	return type;
    }
    
    Address get_base_address() const {
	return base_addr;
    }
    
    const VSRCommand& get_command() const {
	return cmd;
    }
    
    size_t write(void *buf, const size_t buflen, const size_t offset) const {
	size_t off;
	uint32_t w;
	w = type;
	if ((off = write_uint32(w, buf, buflen, offset)) == 0)
	    return 0;
	switch (type) {
	case HANDSHAKE:
	    if ((off = base_addr.write(buf, buflen, off)) == 0)
		return 0;
	    break;
	case CONTROL:
	    if ((off = cmd.write(buf, buflen, off)) == 0)
		return 0;
	    break;
	case VSPROTO:
	    break;
	}
	if (type == CONTROL)
	    assert(off == offset + 12);
	return off;
    }
    
    size_t read(const void *buf, const size_t buflen, const size_t offset) {
	size_t off;
	uint32_t w;
	if ((off = read_uint32(buf, buflen, offset, &w)) == 0)
	    return 0;
	type = static_cast<Type>(w & 0xff);
	switch (type) {
	case HANDSHAKE:
	    if ((off = base_addr.read(buf, buflen, off)) == 0)
		return 0;
	    break;
	case CONTROL:
	    if ((off = cmd.read(buf, buflen, off)) == 0)
		return 0;
	    break;
	case VSPROTO:
	    break;
	}
	
	if ((raw_len = write(raw, sizeof(raw), 0)) == 0)
	    throw DException("");
	return off;
    }
    
    size_t size() const {
	return raw_len;
    }

    const void *get_raw() const {
	return raw;
    }
    const size_t get_raw_len() const {
	return raw_len;
    }
};


class VSRBackend : public VSBackend {
    Transport *tp;
    Poll *poll;
public:
    enum State {CLOSED, CONNECTING, HANDSHAKE, CONNECTED, FAILED} state;
    State get_state() const {
	return state;
    }
    VSRBackend(Poll *, Protolay *);
    ~VSRBackend();
    void handle_up(const int cid, const ReadBuf *, const size_t roff,
		   const ProtoUpMeta *);
    int handle_down(WriteBuf *, const ProtoDownMeta *);
    void connect(const char *);
    void close();
    void join(const ServiceId sid);
    void leave(const ServiceId sid);
};


#endif // VS_REMOTE_BACKEND_HPP
