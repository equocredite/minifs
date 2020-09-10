#ifndef NET_IO_H
#define NET_IO_H

void send_nbytes(const void* buf, int n);

void send_msg(const char* buf);

void send_success();

void send_failure(const char* msg);

int recv_nbytes(void* buf, int n);

int recv_msg(char buf[]);

void discard_msg();


#endif // NET_IO_H
