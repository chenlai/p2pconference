#include <string.h>
#include <stdio.h>
#include <stdlib.h>
/*#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/types.h>*/
//#include <sys/socket.h>
#include <unistd.h>
#include <Transport.h>


Transport::Transport():m_socket(0) {
    memset((char *) &m_sockaddr_in, 0, sizeof(m_sockaddr_in));
    memset((char *) &m_sockaddr_out, 0, sizeof(m_sockaddr_out));
}

Transport::~Transport(){
	close_connection();
}

int Transport::init(int local_port, int remote_port, char *pDestnIP) {
    if ((m_socket=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))==-1) {
        return -1 ;
    }
	//diep("socket");
    m_sockaddr_in.sin_family = AF_INET;
    m_sockaddr_in.sin_port = htons(local_port);
    m_sockaddr_in.sin_addr.s_addr = htonl(INADDR_ANY);

    m_sockaddr_out.sin_family = AF_INET;
    m_sockaddr_out.sin_port = htons(remote_port);
    inet_aton(pDestnIP, &m_sockaddr_out.sin_addr);
    if (bind(m_socket, (struct sockaddr *)&m_sockaddr_in, sizeof(m_sockaddr_in))==-1) {
        return -1 ;
    }

    return m_socket ;
}


int Transport::Send(char *pdata, int length){
    int send_len, slen=sizeof(m_sockaddr_out) ;

    //TBD: populate m_sockaddr_out
    send_len = sendto(m_socket, pdata, length, 0, (struct sockaddr *)&m_sockaddr_out,
		             slen);
    return send_len ;
}

int Transport::Receive(char *pdata, int length){
   struct sockaddr si_other = {0, };
   int recvd_len, slen=sizeof(si_other) ;

   recvd_len = recvfrom(m_socket, pdata, length, 0, (struct sockaddr *)&si_other,
		               &slen);
   return recvd_len ;
}

void Transport::close_connection() {
    close (m_socket);
}

