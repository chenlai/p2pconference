
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>

#ifndef _TRANSPOR_
#define _TRANSPOR_

class Transport {

public:
   Transport();
   ~Transport() ;

   int init (int local_port, int remote_port, char *pDestnIP);
   int Send(char *data, int length);
   int Receive(char *data, int length) ;
   void close_connection ();

private:
   struct sockaddr_in m_sockaddr_in ;
   struct sockaddr_in m_sockaddr_out ;
   int m_socket ;
};

#endif //_TRANSPOR_
