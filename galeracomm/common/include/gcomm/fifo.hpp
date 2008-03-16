#ifndef FIFO_HPP
#define FIFO_HPP

#include <gcomm/readbuf.hpp>
#include <gcomm/writebuf.hpp>

#include <set>
#include <map>
#include <deque>
#include <cerrno>


class Fifo : public std::deque<ReadBuf *> {
    size_t mque_max_size;
    static void release(ReadBuf *rb) {
	rb->release();
    }
    int read_fd;
    int write_fd;
    
    static size_t max_fds;
    static int last_fd;
    static std::map<int, Fifo *> fifo_map;
    static int alloc_fd(Fifo *);
    static void release_fd(const int);
public:
    Fifo() : mque_max_size(std::numeric_limits<size_t>::max()) {
	read_fd = alloc_fd(this);
	write_fd = alloc_fd(this);

    }
    Fifo(const size_t ms) : mque_max_size(ms) {
	read_fd = alloc_fd(this);
	write_fd = alloc_fd(this);
    }
    ~Fifo() {
	release_fd(read_fd);
	release_fd(write_fd);
	for_each(begin(), end(), release);
    }
    int get_read_fd() const {return read_fd;}
    int get_write_fd() const {return write_fd;}
    
    int push_back(const WriteBuf *);
    int push_front(const WriteBuf *);
    int push_after(Fifo::iterator, const WriteBuf *);
    
    ReadBuf *pop_front();

    
    bool is_empty() const {
	return size() == 0;
    }
    
    bool is_full() const {
	return size() == mque_max_size;
    }
    

    
    static Fifo *find(const int fd);
};

#endif /* FIFO_HPP */
