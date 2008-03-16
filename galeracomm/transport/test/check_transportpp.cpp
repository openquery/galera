

#include "gcomm/transport.hpp"
#include "gcomm/exception.hpp"

#include <check.h>
#include <cstdlib>
#include <list>
#include <iostream>

const char *addr = "tcp:127.0.0.1:23456";


class Sender : Toplay {
    Transport *tp;
    Poll *poll;
    bool can_send;
public:
    Sender(Poll *p) : tp(0), poll(p), can_send(false) {}
    void handle_up(const int cid, const ReadBuf *rb, const size_t offset, 
		   const ProtoUpMeta *um) {
	if (rb == 0 && tp->get_state() == TRANSPORT_S_CONNECTED)
	    can_send = true;
	else
	    throw DException("");
    }

    bool get_connected() const {
	return can_send;
    }
    void connect() {
	tp = Transport::create(addr, poll, this);
	set_down_context(tp);
	tp->connect(addr);
	tp->set_max_pending_bytes(1 << 31);
    }
    bool send(const size_t b) {
	unsigned char *buf = new unsigned char[b];
	if (can_send == false)
	    throw DException("");
	
	for (size_t i = 0; i < b; i++)
	    buf[i] = i % 256;
	WriteBuf *wb = new WriteBuf(buf, b);
	int res;
	bool ret = (res = pass_down(wb, 0)) == 0 ? true : false;
	delete wb;
	delete[] buf;
	return ret;
    }
    void send_sync(const size_t b) {
	unsigned char *buf = new unsigned char[b];
	if (can_send == false)
	    throw DException("");
	for (size_t i = 0; i < b; i++)
	    buf[i] = i % 256;
	WriteBuf *wb = new WriteBuf(buf, b);
	tp->send(wb, 0);
	delete wb;
	delete[] buf;
    }
    void close() {
	tp->close();
	delete tp;
	tp = 0;
	can_send = false;
    }
};




class Receiver : public Toplay {
    clock_t cstart;
    clock_t cstop;
    uint64_t recvd;
    Transport *tp;
public:
    Receiver(Transport *t) : recvd(0), tp(t) {
	cstart = clock();
	tp->set_up_context(this);
    }
    ~Receiver() {
	cstop = clock();
	clock_t ct = (cstop - cstart);
	double tput = CLOCKS_PER_SEC*double(recvd)/(cstop - cstart);
	std::cerr << "Reciver: received " << recvd << " bytes\n";
	std::cerr << "         used " << (double(ct)/CLOCKS_PER_SEC) << " secs cputime\n";
	std::cerr << "         throughput " << tput << "bytes/cpusec\n";
    }
    void handle_up(const int cid, const ReadBuf *rb, const size_t offset,
		   const ProtoUpMeta *um) {
	const unsigned char *ptr;
	if (rb == 0) {
	    if (tp->get_state() == TRANSPORT_S_FAILED)
		throw DException(strerror(tp->get_errno()));
	    return;
	}
	for (size_t i = rb->get_len(); i < rb->get_len() - offset;
	     i++) {
	    ptr = reinterpret_cast<const unsigned char *>(rb->get_buf(i));
	    if (*ptr != i % 256)
		throw DException("");
	}
	
	recvd += rb->get_len() - offset;
    }
    void recv() {
	const ReadBuf *rb = tp->recv();
	if (rb == 0)
	    throw DException("");
	const unsigned char *ptr;
	for (size_t i = rb->get_len(); i < rb->get_len(); i++) {
	    ptr = reinterpret_cast<const unsigned char *>(rb->get_buf(i));
	    if (*ptr != i % 256)
		throw DException("");
	}
    }
};

void release(std::pair<Receiver *, Transport *>& p)
{
    delete p.first;
    delete p.second;
}

class Listener : public Toplay {
    Transport *tp;
    Poll *poll;
    std::list<std::pair<Receiver *, Transport *> > tports;
public:
    Receiver *get_first_receiver() {
	return tports.front().first;
    }
    Listener(Poll *_poll) : tp(0), poll(_poll) {}
    ~Listener() {
	delete tp;
	tp = 0;
	for_each(tports.begin(), tports.end(), release);
	tports.clear();
    }
    
    void handle_up(const int cid, const ReadBuf *rb, const size_t offset, 
		   const ProtoUpMeta *um) {
	

	Transport *t = tp->accept(poll);
	Receiver *r = new Receiver(t);
	tports.push_back(std::pair<Receiver *, Transport *>(r, t));
    }

    int handle_down(WriteBuf *wb, const ProtoDownMeta *dm) {
	throw DException("");
	return 0;
    }
    
    void start() {
	if (tp)
	    throw DException("");
	tp = Transport::create(addr, poll, this);
	tp->listen(addr);
    }
    
    void stop() {
	delete tp;
	tp = 0;
	for_each(tports.begin(), tports.end(), release);
	tports.clear();
    }
};

START_TEST(check_transport)
{
    Poll *p = Poll::create("Def");
    Listener l(p);
    Sender s(p);
    l.start();
    s.connect();
    p->poll(1);

    while (s.get_connected() == false) {
	p->poll(1);
    }
    
    for (size_t i = 1; i <= (1 << 24);) {
	if (s.send(i) == true)
	    i *= 2;
	p->poll(1);
    }
        
    while (p->poll(1) > 0);

    Receiver *r = l.get_first_receiver();
    if (r == 0)
	throw DException("");
    
    for (size_t i = 0; i < 100; ++i) {
	s.send(1024);
	r->recv();
    }
    std::cerr << "Terminating\n";

    s.close();
    l.stop();
    delete p;
}
END_TEST

START_TEST(check_multitransport)
{
    Poll *p = Poll::create("Def");
    Listener l(p);
    Sender s1(p), s2(p), s3(p);
    l.start();
    s1.connect();
    s2.connect();
    s3.connect();
    p->poll(1);
    
    while (s1.get_connected() == false || 
	   s2.get_connected() == false ||
	   s3.get_connected() == false) {
	p->poll(1);
    }
    
    for (int i = 0; i < 10000; i++) {
	s1.send(rand()%10000);
	s2.send(rand()%10000);
	s3.send(rand()%10000);
	p->poll(1);
    }
    
    while (p->poll(1) > 0);
    
    std::cerr << "Terminating\n";
    
    s1.close();
    s2.close();
    s3.close();
    l.stop();
    delete p;
}
END_TEST



static Suite *suite()
{
    Suite *s = suite_create("transportpp");
    TCase *tc;

    tc = tcase_create("check_transport");
    tcase_add_test(tc, check_transport);
    suite_add_tcase(s, tc);

    tc = tcase_create("check_multitransport");
    tcase_add_test(tc, check_multitransport);
    suite_add_tcase(s, tc);

    return s;
}

int main()
{
    Suite *s;
    SRunner *sr;

    s = suite();
    sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    int n_fail = srunner_ntests_failed(sr);
    srunner_free(sr);
    return n_fail == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
