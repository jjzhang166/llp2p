
#include "io_accept_nat.h"
#include "pk_mgr.h"
#include "network.h"
#include "logger.h"
#include "peer_mgr.h"
#include "peer.h"
#include "peer_communication.h"
#include "logger_client.h"
#include "io_nonblocking.h"
#include "stunt_mgr.h"

using namespace std;

io_accept_nat::io_accept_nat(network *net_ptr,logger *log_ptr,configuration *prep_ptr,peer_mgr * peer_mgr_ptr,peer *peer_ptr,pk_mgr * pk_mgr_ptr, peer_communication *peer_communication_ptr, logger_client * logger_client_ptr, stunt_mgr *stunt_mgr_ptr){
	_net_ptr = net_ptr;
	_log_ptr = log_ptr;
	_prep = prep_ptr;
	_peer_mgr_ptr = peer_mgr_ptr;
	_peer_ptr = peer_ptr;
	_pk_mgr_ptr = pk_mgr_ptr;
	_peer_communication_ptr = peer_communication_ptr;
	_logger_client_ptr = logger_client_ptr;
	_stunt_mgr_ptr = stunt_mgr_ptr;
}

io_accept_nat::~io_accept_nat(){
	printf("==============deldet io_accept_nat success==========\n");

}


//should change to non-blocking
int io_accept_nat::handle_pkt_in(int sock)
{	
	/*
	accept new peer fd, recv protocol to identify candidate or not.
	save its role(candidate or rescue peer) and bind to peer_com~ for handle_pkt_in/out.
	*/
	printf("----------io_accept_nat::handle_pkt_in \n");
	socklen_t sin_len = sizeof(struct sockaddr_in);
	struct chunk_header_t *chunk_header_ptr = NULL;
	int expect_len;
	int recv_byte,offset,buf_len;
	struct chunk_t *chunk_ptr = NULL;

	offset = 0;
	int new_fd = _net_ptr->accept(sock, (struct sockaddr *)&_cin, &sin_len);
	
	struct sockaddr_in addr;
	int addrLen=sizeof(struct sockaddr_in),
		aa;
	aa=getpeername(new_fd, (struct sockaddr *)&addr, &addrLen);
	//printf("  aa:%2d  new_fd: %2d , DstAddr: %s:%d  ", aa, new_fd, inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));

	
	
	_log_ptr->write_log_format("s =>u s d s\n", __FUNCTION__,__LINE__,"[NAT ACCEPT ++]", aa, inet_ntoa(addr.sin_addr));
	_logger_client_ptr->log_to_server(LOG_WRITE_STRING,0,"s \n", "Build TCP Hole Punching connection accept");
	
	printf("new_fd: %d   %d \n", new_fd, WSAGetLastError());
	if(new_fd < 0) {
		return RET_SOCK_ERROR;
	}else {

		_peer_communication_ptr->map_fd_NonBlockIO_iter =_peer_communication_ptr->map_fd_NonBlockIO.find(new_fd);
		
		// Check wheather new_fd is already in the map_fd_NonBlockIO or not
		if(_peer_communication_ptr->map_fd_NonBlockIO_iter ==_peer_communication_ptr->map_fd_NonBlockIO.end() ){
			struct ioNonBlocking* ioNonBlocking_ptr =new struct ioNonBlocking;
			if(!ioNonBlocking_ptr){
				printf("ioNonBlocking_ptr new error \n");
				_log_ptr->write_log_format("s =>u s  \n", __FUNCTION__,__LINE__," ioNonBlocking_ptr new error");
				PAUSE
			}
			memset(ioNonBlocking_ptr,0x00,sizeof(struct ioNonBlocking));
			ioNonBlocking_ptr->io_nonblockBuff.nonBlockingRecv.recv_packet_state =READ_HEADER_READY;
	//		printf("ioNonBlocking_ptr->io_nonblockBuff.nonBlockingRecv.recv_packet_state = %d\n ",ioNonBlocking_ptr->io_nonblockBuff.nonBlockingRecv.recv_packet_state);
			printf("_peer_communication_ptr->map_fd_NonBlockIO.size: %d \n", _peer_communication_ptr->map_fd_NonBlockIO.size());
			_peer_communication_ptr->map_fd_NonBlockIO[new_fd] =ioNonBlocking_ptr;
			printf("_peer_communication_ptr->map_fd_NonBlockIO.size: %d \n", _peer_communication_ptr->map_fd_NonBlockIO.size());
			
		}else{
			printf("fd=%d dup in _peer_communication_ptr->map_fd_NonBlockIO_iter  error\n",new_fd);

		}


		//_net_ptr->set_blocking(new_fd);
		cout << "new_fd = " << new_fd << endl;   
		//PAUSE

		_net_ptr->set_nonblocking(new_fd);

		_net_ptr->epoll_control(new_fd, EPOLL_CTL_ADD, EPOLLIN );
		_net_ptr->set_fd_bcptr_map(new_fd, dynamic_cast<basic_class *> (_peer_communication_ptr->_io_nonblocking_ptr));
		_peer_mgr_ptr->fd_list_ptr->push_back(new_fd);

		// Delete listen-socket in map
		_net_ptr->epoll_control(sock, EPOLL_CTL_DEL, 0);
		_net_ptr->fd_bcptr_map_delete(sock);
		_net_ptr->eraseFdList(sock);
		closesocket(sock);

	}
	printf("io_accept_nat::handle_pkt_in end \n");
	return RET_OK;
}

int io_accept_nat::handle_pkt_out(int sock)
{
	/*
	we will not inside this part
	*/
	_logger_client_ptr->log_to_server(LOG_WRITE_STRING,0,"s \n","error : place in io_accept_nat::handle_pkt_out\n");
	_logger_client_ptr->log_exit();
	return RET_OK;
}

void io_accept_nat::handle_pkt_error(int sock)
{
	_logger_client_ptr->log_to_server(LOG_WRITE_STRING,0,"s d \n","error in io_accept_nat handle_pkt_error error number : ",WSAGetLastError());
	_logger_client_ptr->log_exit();
}

void io_accept_nat::handle_job_realtime()
{

}


void io_accept_nat::handle_job_timer()
{

}

void io_accept_nat::handle_sock_error(int sock, basic_class *bcptr){
	
	_peer_communication_ptr->fd_close(sock);
	_logger_client_ptr->log_to_server(LOG_WRITE_STRING,0,"s d \n","error in io_accept_nat handle_sock_error error number : ",WSAGetLastError());
	_logger_client_ptr->log_exit();
}