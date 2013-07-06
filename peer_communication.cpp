
#include "peer_communication.h"
#include "pk_mgr.h"
#include "network.h"
#include "logger.h"
#include "peer_mgr.h"
#include "peer.h"
#include "io_accept.h"
#include "io_connect.h"
#include "logger_client.h"
#include "io_nonblocking.h"

using namespace std;

peer_communication::peer_communication(network *net_ptr,logger *log_ptr,configuration *prep_ptr,peer_mgr * peer_mgr_ptr,peer *peer_ptr,pk_mgr * pk_mgr_ptr, logger_client * logger_client_ptr){
	_net_ptr = net_ptr;
	_log_ptr = log_ptr;
	_prep = prep_ptr;
	_peer_mgr_ptr = peer_mgr_ptr;
	_peer_ptr = peer_ptr;
	_pk_mgr_ptr = pk_mgr_ptr;
	_logger_client_ptr = logger_client_ptr;
	total_manifest = 0;
	session_id_count = 0;
	self_info =NULL;
	self_info = new struct level_info_t;
	_io_accept_ptr =NULL;
	_io_connect_ptr =NULL;
	_io_nonblocking_ptr=NULL;
	_io_nonblocking_ptr = new io_nonblocking(net_ptr,log_ptr ,this,logger_client_ptr);
	_io_accept_ptr = new io_accept(net_ptr,log_ptr,prep_ptr,peer_mgr_ptr,peer_ptr,pk_mgr_ptr,this,logger_client_ptr);
	_io_connect_ptr = new io_connect(net_ptr,log_ptr,prep_ptr,peer_mgr_ptr,peer_ptr,pk_mgr_ptr,this,logger_client_ptr);
	fd_list_ptr =NULL;
	fd_list_ptr = pk_mgr_ptr->fd_list_ptr ;
	//peer_com_log = fopen("./peer_com_log.txt","wb");
}

peer_communication::~peer_communication(){

	if(self_info)
		delete self_info;

	if(_io_accept_ptr)
		delete _io_accept_ptr;
	if(_io_connect_ptr)
		delete _io_connect_ptr;
	if(_io_nonblocking_ptr)
		delete _io_nonblocking_ptr;
	_io_accept_ptr =NULL;
	_io_connect_ptr =NULL;
	_io_nonblocking_ptr=NULL;

	for(session_id_candidates_set_iter = session_id_candidates_set.begin() ;session_id_candidates_set_iter !=session_id_candidates_set.end();session_id_candidates_set_iter){
		
		for(int i=0;i< session_id_candidates_set_iter->second->peer_num;i++)
			delete session_id_candidates_set_iter->second->list_info->level_info[i];
		delete session_id_candidates_set_iter->second->list_info;
		delete session_id_candidates_set_iter->second;
	}
	session_id_candidates_set.clear();


	for(map_fd_info_iter=map_fd_info.begin() ; map_fd_info_iter!=map_fd_info.end();map_fd_info_iter++){
		delete map_fd_info_iter->second;
	}
	map_fd_info.clear();



	for(map_fd_NonBlockIO_iter=map_fd_NonBlockIO.begin() ; map_fd_NonBlockIO_iter!=map_fd_NonBlockIO.end();map_fd_NonBlockIO_iter++){
		delete map_fd_NonBlockIO_iter->second;
	}
	map_fd_NonBlockIO.clear();

	printf("==============deldet peer_communication success==========\n");
}

void peer_communication::set_self_info(unsigned long public_ip){
	self_info->public_ip = public_ip;
	self_info->private_ip = _net_ptr->getLocalIpv4();
}

int peer_communication::set_candidates_handler(unsigned long rescue_manifest,struct chunk_level_msg_t *testing_info,unsigned int candidates_num,int flag){	//flag 0 rescue peer, flag 1 candidate's peer
	
	_log_ptr->write_log_format("s =>u \n", __FUNCTION__,__LINE__);
	_log_ptr->write_log_format("s =>u s s d s d s d s d\n", __FUNCTION__,__LINE__,"set_candidates_handler","session_id : ",session_id_count,", manifest : ",rescue_manifest,", role: ",flag,", list_number: ",candidates_num);
	for(int i=0;i<candidates_num;i++){
		//fprintf(peer_com_log,"list pid : %d, public_ip : %d, private_ip: %d\n",testing_info->level_info[i]->pid,testing_info->level_info[i]->public_ip,testing_info->level_info[i]->private_ip);
		_log_ptr->write_log_format("s =>u s d\n", __FUNCTION__,__LINE__,"list pid : ",testing_info->level_info[i]->pid);
	}

//candidates_num always not zero
	if((candidates_num==0)&&(flag==0)){
		_log_ptr->write_log_format("s =>u s \n", __FUNCTION__,__LINE__,"rescue peer call peer API, but list is empty");
		if((total_manifest & rescue_manifest)==1){
			
			_logger_client_ptr->log_to_server(LOG_WRITE_STRING,0,"s d d \n","error : re-rescue for some sub stream : %d %d in set_candidates_test",total_manifest,rescue_manifest);
			_logger_client_ptr->log_exit();
		}
		else{
			printf("rescue manifest: %d already rescue manifest: %d\n",rescue_manifest,total_manifest);
			_log_ptr->write_log_format("s =>u s d s d\n", __FUNCTION__,__LINE__,"rescue manifest: ",rescue_manifest," already rescue manifest: ",total_manifest);
			total_manifest = total_manifest | rescue_manifest;	//total_manifest has to be erased in stop_attempt_connect
		
			session_id_candidates_set_iter = session_id_candidates_set.find(session_id_count);	//manifest_candidates_set has to be erased in stop_attempt_connect
			if(session_id_candidates_set_iter != session_id_candidates_set.end()){
				
				_logger_client_ptr->log_to_server(LOG_WRITE_STRING,0,"s \n","error : session id already in the record in set_candidates_test");
				_logger_client_ptr->log_exit();
			}
			else{
				session_id_candidates_set[session_id_count] = new struct peer_com_info;

				session_id_candidates_set_iter = session_id_candidates_set.find(session_id_count);	//manifest_candidates_set has to be erased in stop_attempt_connect
				if(session_id_candidates_set_iter == session_id_candidates_set.end()){
					
					_logger_client_ptr->log_to_server(LOG_WRITE_STRING,0,"s \n","error : session id cannot find in the record in set_candidates_test\n");
					_logger_client_ptr->log_exit();
				}

				int level_msg_size,offset;
				offset = 0;
				level_msg_size = sizeof(struct chunk_header_t) + sizeof(unsigned long) + sizeof(unsigned long) + candidates_num * sizeof(struct level_info_t *);

				session_id_candidates_set_iter->second->peer_num = candidates_num;
				session_id_candidates_set_iter->second->manifest = rescue_manifest;
				session_id_candidates_set_iter->second->role = 0;
				session_id_candidates_set_iter->second->list_info = (struct chunk_level_msg_t *) new unsigned char[level_msg_size];
				memset(session_id_candidates_set_iter->second->list_info, 0x0, level_msg_size);
				memcpy(session_id_candidates_set_iter->second->list_info, testing_info, (level_msg_size - candidates_num * sizeof(struct level_info_t *)));

				offset += (level_msg_size - candidates_num * sizeof(struct level_info_t *));

				for(int i=0;i<candidates_num;i++){
					session_id_candidates_set_iter->second->list_info->level_info[i] = new struct level_info_t;
					memset(session_id_candidates_set_iter->second->list_info->level_info[i], 0x0 , sizeof(struct level_info_t));
					memcpy(session_id_candidates_set_iter->second->list_info->level_info[i], testing_info->level_info[i] , sizeof(struct level_info_t));
					offset += sizeof(struct level_info_t);
				}
			}
			session_id_count++;
		}
		return (session_id_count-1);
	}
	else if((candidates_num==0)&&(flag==1)){
		
		_logger_client_ptr->log_to_server(LOG_WRITE_STRING,0,"s \n","unknow state in set_candidates_handler\n");
		_logger_client_ptr->log_exit();
	}


	//Child
	if(flag == 0){
		_log_ptr->write_log_format("s =>u s \n", __FUNCTION__,__LINE__,"rescue peer call peer API\n");
		if((total_manifest & rescue_manifest)==1){
			
			_logger_client_ptr->log_to_server(LOG_WRITE_STRING,0,"s d d \n","error : re-rescue for some sub stream : %d %d in set_candidates_test\n",total_manifest,rescue_manifest);
			_logger_client_ptr->log_exit();
		}
		else{
			printf("rescue manifest: %d already rescue manifest: %d\n",rescue_manifest,total_manifest);
			_log_ptr->write_log_format("s =>u s d s d\n", __FUNCTION__,__LINE__,"rescue manifest: ",rescue_manifest," already rescue manifest: ",total_manifest);
			total_manifest = total_manifest | rescue_manifest;	//total_manifest has to be erased in stop_attempt_connect
		
			session_id_candidates_set_iter = session_id_candidates_set.find(session_id_count);	//manifest_candidates_set has to be erased in stop_attempt_connect
			if(session_id_candidates_set_iter != session_id_candidates_set.end()){
				
				_logger_client_ptr->log_to_server(LOG_WRITE_STRING,0,"s \n","error : session id already in the record in set_candidates_test\n");
				_logger_client_ptr->log_exit();
			}
			else{
				session_id_candidates_set[session_id_count] = new struct peer_com_info;

				session_id_candidates_set_iter = session_id_candidates_set.find(session_id_count);	//manifest_candidates_set has to be erased in stop_attempt_connect
				if(session_id_candidates_set_iter == session_id_candidates_set.end()){
					
					_logger_client_ptr->log_to_server(LOG_WRITE_STRING,0,"s \n","error : session id cannot find in the record in set_candidates_test\n");
					_logger_client_ptr->log_exit();
				}

				int level_msg_size,offset;
				offset = 0;
				level_msg_size = sizeof(struct chunk_header_t) + sizeof(unsigned long) + sizeof(unsigned long) + candidates_num * sizeof(struct level_info_t *);

				session_id_candidates_set_iter->second->peer_num = candidates_num;
				session_id_candidates_set_iter->second->manifest = rescue_manifest;
				session_id_candidates_set_iter->second->role = 0;
				session_id_candidates_set_iter->second->list_info = (struct chunk_level_msg_t *) new unsigned char[level_msg_size];
				memset(session_id_candidates_set_iter->second->list_info, 0x0, level_msg_size);
				memcpy(session_id_candidates_set_iter->second->list_info, testing_info, (level_msg_size - candidates_num * sizeof(struct level_info_t *)));

				offset += (level_msg_size - candidates_num * sizeof(struct level_info_t *));

				for(int i=0;i<candidates_num;i++){
					session_id_candidates_set_iter->second->list_info->level_info[i] = new struct level_info_t;
					memset(session_id_candidates_set_iter->second->list_info->level_info[i], 0x0 , sizeof(struct level_info_t));
					memcpy(session_id_candidates_set_iter->second->list_info->level_info[i], testing_info->level_info[i] , sizeof(struct level_info_t));
					offset += sizeof(struct level_info_t);
				}

				for(int i=0;i<candidates_num;i++){
					if((self_info->private_ip == self_info->public_ip)&&(testing_info->level_info[i]->private_ip ==testing_info->level_info[i]->public_ip)){	//self public ip , candidate public ip
						printf("all public ip active connect\n");
						_log_ptr->write_log_format("s =>u s \n", __FUNCTION__,__LINE__,"all public ip active connect");
						non_blocking_build_connection(testing_info->level_info[i],0,rescue_manifest,testing_info->level_info[i]->pid,0,session_id_count);
					}
					else if((self_info->private_ip == self_info->public_ip)&&(testing_info->level_info[i]->private_ip !=testing_info->level_info[i]->public_ip)){	//self public ip , candidate private ip
						printf("candidate is private ip passive connect\n");
						_log_ptr->write_log_format("s =>u s \n", __FUNCTION__,__LINE__,"candidate is private ip passive connect");
						accept_check(testing_info->level_info[i],0,rescue_manifest,testing_info->level_info[i]->pid,session_id_count);
					}
					else if((self_info->private_ip != self_info->public_ip)&&(testing_info->level_info[i]->private_ip ==testing_info->level_info[i]->public_ip)){	//self private ip , candidate public ip
						printf("rescue peer is private ip active connect\n");
						_log_ptr->write_log_format("s =>u s \n", __FUNCTION__,__LINE__,"rescue peer is public ip active connect");
						non_blocking_build_connection(testing_info->level_info[i],0,rescue_manifest,testing_info->level_info[i]->pid,0,session_id_count);
					}
					else if((self_info->private_ip != self_info->public_ip)&&(testing_info->level_info[i]->private_ip !=testing_info->level_info[i]->public_ip)){	//self private ip , candidate private ip
						printf("all private ip use NAT module\n");
						_log_ptr->write_log_format("s =>u s \n", __FUNCTION__,__LINE__,"all private ip use NAT module");
						if(self_info->public_ip == testing_info->level_info[i]->public_ip){
							printf("same NAT device active connect\n");
							_log_ptr->write_log_format("s =>u s \n", __FUNCTION__,__LINE__,"same NAT device active connect");
							non_blocking_build_connection(testing_info->level_info[i],0,rescue_manifest,testing_info->level_info[i]->pid,1,session_id_count);
						}
					}
					else{
						
						_logger_client_ptr->log_to_server(LOG_WRITE_STRING,0,"s \n","error : unknown state in rescue peer\n");
						_logger_client_ptr->log_exit();
					}
				}
			}
			session_id_count++;
		}
	
	// Parent
	}
	else if(flag == 1){
		_log_ptr->write_log_format("s =>u s \n", __FUNCTION__,__LINE__,"candidate calls peer_com API");
		if((candidates_num>1)||((candidates_num==0))){
			
			_logger_client_ptr->log_to_server(LOG_WRITE_STRING,0,"s \n","error : candidate num must be 1\n");
			_logger_client_ptr->log_exit();
		}
		else{
			printf("candidate manifest: %d \n",rescue_manifest);
			_log_ptr->write_log_format("s =>u s d\n", __FUNCTION__,__LINE__,"candidate manifest: ",rescue_manifest);
		
			session_id_candidates_set_iter = session_id_candidates_set.find(session_id_count);	//manifest_candidates_set has to be erased in stop_attempt_connect
			if(session_id_candidates_set_iter != session_id_candidates_set.end()){
				
				_logger_client_ptr->log_to_server(LOG_WRITE_STRING,0,"s \n","error : session id in the record in set_candidates_test\n");
				_logger_client_ptr->log_exit();
			}
			else{
				session_id_candidates_set[session_id_count] = new struct peer_com_info;

				session_id_candidates_set_iter = session_id_candidates_set.find(session_id_count);	//manifest_candidates_set has to be erased in stop_attempt_connect
				if(session_id_candidates_set_iter == session_id_candidates_set.end()){
					
					_logger_client_ptr->log_to_server(LOG_WRITE_STRING,0,"s \n","error : session id cannot find in the record in set_candidates_test\n");
					_logger_client_ptr->log_exit();
				}

				int level_msg_size,offset;
				offset = 0;
				level_msg_size = sizeof(struct chunk_header_t) + sizeof(unsigned long) + sizeof(unsigned long) + candidates_num * sizeof(struct level_info_t *);

				session_id_candidates_set_iter->second->peer_num = candidates_num;
				session_id_candidates_set_iter->second->manifest = rescue_manifest;
				session_id_candidates_set_iter->second->role = 1;
				session_id_candidates_set_iter->second->list_info = (struct chunk_level_msg_t *) new unsigned char[level_msg_size];
				memset(session_id_candidates_set_iter->second->list_info, 0x0, level_msg_size);
				memcpy(session_id_candidates_set_iter->second->list_info, testing_info, (level_msg_size - candidates_num * sizeof(struct level_info_t *)));

				offset += (level_msg_size - candidates_num * sizeof(struct level_info_t *));

				for(int i=0;i<candidates_num;i++){
					session_id_candidates_set_iter->second->list_info->level_info[i] = new struct level_info_t;
					memset(session_id_candidates_set_iter->second->list_info->level_info[i], 0x0 , sizeof(struct level_info_t));
					memcpy(session_id_candidates_set_iter->second->list_info->level_info[i], testing_info->level_info[i] , sizeof(struct level_info_t));
					offset += sizeof(struct level_info_t);
				}
				if((self_info->private_ip == self_info->public_ip)&&(testing_info->level_info[0]->private_ip ==testing_info->level_info[0]->public_ip)){	//self public ip , rescue peer public ip
					printf("all public ip passive connect in cnadidate\n");
					_log_ptr->write_log_format("s =>u s \n", __FUNCTION__,__LINE__,"all public ip passive connect in cnadidate");
					accept_check(testing_info->level_info[0],1,rescue_manifest,testing_info->level_info[0]->pid,session_id_count);
				}
				else if((self_info->private_ip == self_info->public_ip)&&(testing_info->level_info[0]->private_ip !=testing_info->level_info[0]->public_ip)){	//self public ip , rescue peer private ip
					printf("rescue peer is private ip passive connect in cnadidate\n");
					_log_ptr->write_log_format("s =>u s \n", __FUNCTION__,__LINE__,"rescue peer is private ip passive connect in cnadidate");
					accept_check(testing_info->level_info[0],1,rescue_manifest,testing_info->level_info[0]->pid,session_id_count);
				}
				else if((self_info->private_ip != self_info->public_ip)&&(testing_info->level_info[0]->private_ip ==testing_info->level_info[0]->public_ip)){	//self private ip , rescue peer public ip
					printf("candidate is private ip active connect in cnadidate\n");
					_log_ptr->write_log_format("s =>u s \n", __FUNCTION__,__LINE__,"candidate is private ip active connect in cnadidate");
					non_blocking_build_connection(testing_info->level_info[0],1,rescue_manifest,testing_info->level_info[0]->pid,0,session_id_count);
				}
				else if((self_info->private_ip != self_info->public_ip)&&(testing_info->level_info[0]->private_ip !=testing_info->level_info[0]->public_ip)){	//self private ip , rescue peer private ip
					printf("all private ip use NAT module in cnadidate\n");
					_log_ptr->write_log_format("s =>u s \n", __FUNCTION__,__LINE__,"all private ip use NAT module in cnadidate");
					if(self_info->public_ip == testing_info->level_info[0]->public_ip){
							printf("same NAT device passive connect in cnadidate\n");
							_log_ptr->write_log_format("s =>u s \n", __FUNCTION__,__LINE__,"same NAT device passive connect in cnadidate");
							accept_check(testing_info->level_info[0],1,rescue_manifest,testing_info->level_info[0]->pid,session_id_count);
					}
				}
				else{
					
					_logger_client_ptr->log_to_server(LOG_WRITE_STRING,0,"s \n","error : unknown state in candidate's peer\n");
					_logger_client_ptr->log_exit();
				}
			}
			session_id_count++;
		}
	}
	else{
		
		_logger_client_ptr->log_to_server(LOG_WRITE_STRING,0,"s \n","error : unknow flag in set_candidates_test\n");
		_logger_client_ptr->log_exit();
	}

	return (session_id_count-1);
}

void peer_communication::clear_fd_in_peer_com(int sock){
	_log_ptr->write_log_format("s =>u \n", __FUNCTION__,__LINE__);
	_log_ptr->write_log_format("s =>u s d \n", __FUNCTION__,__LINE__,"start close in peer_communication::clear_fd_in_peer_com fd : ",sock);

	map_fd_NonBlockIO_iter = map_fd_NonBlockIO.find(sock);
	if(map_fd_NonBlockIO_iter != map_fd_NonBlockIO.end()){
		delete map_fd_NonBlockIO_iter ->second;
		map_fd_NonBlockIO.erase(map_fd_NonBlockIO_iter);
	}

	list<int>::iterator map_fd_unknown_iter;

	for(map_fd_unknown_iter = _io_accept_ptr->map_fd_unknown.begin();map_fd_unknown_iter != _io_accept_ptr->map_fd_unknown.end();map_fd_unknown_iter++){
		if( sock == *map_fd_unknown_iter){
			_io_accept_ptr->map_fd_unknown.erase(map_fd_unknown_iter);
			break;
		}
	}



	map_fd_info_iter = map_fd_info.find(sock);
	if(map_fd_info_iter == map_fd_info.end()){
		_log_ptr->write_log_format("s =>u s d\n", __FUNCTION__,__LINE__,"close fail (clear_fd_in_peer_com) fd : ",sock);
	}
	else{
		_log_ptr->write_log_format("s =>u s d s d s d s\n", __FUNCTION__,__LINE__,"fd : ",sock," session id : ",map_fd_info_iter->second->session_id," pid : ",map_fd_info_iter->second->pid," close succeed (clear_fd_in_peer_com)\n");
		
		delete map_fd_info_iter->second;
		map_fd_info.erase(map_fd_info_iter);
	}
	_log_ptr->write_log_format("s =>u \n", __FUNCTION__,__LINE__);
}

void peer_communication::accept_check(struct level_info_t *level_info_ptr,int fd_role,unsigned long manifest,unsigned long fd_pid, unsigned long session_id){
	//map<int, int>::iterator map_fd_unknown_iter;
	list<int>::iterator map_fd_unknown_iter;
	/*map<int, unsigned long>::iterator map_fd_session_id_iter;
	map<int, unsigned long>::iterator map_peer_com_fd_pid_iter;
	map<int, unsigned long>::iterator map_fd_manifest_iter;*/
	
	for(map_fd_unknown_iter = _io_accept_ptr->map_fd_unknown.begin();map_fd_unknown_iter != _io_accept_ptr->map_fd_unknown.end();map_fd_unknown_iter++){
		//if(*map_fd_unknown_iter == 1){
			
			
			map_fd_info_iter = map_fd_info.find(*map_fd_unknown_iter);
			if(map_fd_info_iter == map_fd_info.end()){
				
				_logger_client_ptr->log_to_server(LOG_WRITE_STRING,0,"s \n","cannot find map_fd_info_iter structure in peer_communication::handle_pkt_out\n");
				_logger_client_ptr->log_exit();
			}

			if((manifest == map_fd_info_iter->second->manifest)&&(fd_role == map_fd_info_iter->second->flag)&&(fd_pid == map_fd_info_iter->second->pid)){
				_log_ptr->write_log_format("s =>u s d\n", __FUNCTION__,__LINE__,"update session of fd in peer_communication::accept_check fd : ",*map_fd_unknown_iter);
				printf("fd : %d update session of fd in peer_communication::accept_check\n",*map_fd_unknown_iter);

				map_fd_info_iter->second->session_id = session_id;

				/*
				bind to peer_com~ object
				*/
				_log_ptr->write_log_format("s =>u s d \n", __FUNCTION__,__LINE__,"bind to peer_com in peer_communication::accept_check fd : ",*map_fd_unknown_iter);
				_net_ptr->set_nonblocking(map_fd_info_iter->first);

				//_net_ptr->epoll_control(map_fd_info_iter->first, EPOLL_CTL_ADD, EPOLLIN | EPOLLOUT);
				//_net_ptr->set_fd_bcptr_map(map_fd_info_iter->first, dynamic_cast<basic_class *> (this));
				//_peer_mgr_ptr->fd_list_ptr->push_back(map_fd_info_iter->first);

				_io_accept_ptr->map_fd_unknown.erase(map_fd_unknown_iter);
				break;
			}
		
	}

	/*
	call nat accept check
	*/
}

int peer_communication::non_blocking_build_connection(struct level_info_t *level_info_ptr,int fd_role,unsigned long manifest,unsigned long fd_pid, int flag, unsigned long session_id){	//flag 0 public ip flag 1 private ip //fd_role 0 rescue peer fd_role 1 
	struct sockaddr_in peer_saddr;
	int ret;
	struct in_addr ip;
	int _sock;
	/*
	this part means that if the parent is exist, we don't create it again.
	*/
	//fprintf(peer_com_log,"call non_blocking_build_connection ip : %s , role :%d , manifest : %d , fd_pid : %d , flag : %d\n",level_info_ptr->public_ip,fd_role, manifest, fd_pid, flag);
	_log_ptr->write_log_format("s =>u s d s d s d s d\n", __FUNCTION__,__LINE__,"call non_blocking_build_connection role :",fd_role," , manifest : ", manifest," , fd_pid : ", fd_pid," , flag : ", flag);
	if(fd_role == 0){
		multimap<unsigned long, struct peer_info_t *>::iterator pid_peer_info_iter;
		map<unsigned long, int>::iterator map_pid_fd_iter;
		map<unsigned long, struct peer_connect_down_t *>::iterator pid_peerDown_info_iter;

		//���e�w�g�إ߹L�s�u�� �bmap_in_pid_fd�̭� �h���A�إ�(�O�ҹ�P��parent���A�إ߲ĤG���u)
		for(map_pid_fd_iter = _peer_ptr->map_in_pid_fd.begin();map_pid_fd_iter != _peer_ptr->map_in_pid_fd.end(); map_pid_fd_iter++){
			if(map_pid_fd_iter->first == level_info_ptr->pid ){
				_log_ptr->write_log_format("s =>u s \n", __FUNCTION__,__LINE__,"fd already in map_in_pid_fd in non_blocking_build_connection (rescue peer)");
				return 1;
			}
		}

		/*
		this may have problem****************************************************************
		*/
		pid_peer_info_iter = _pk_mgr_ptr ->map_pid_peer_info.find(level_info_ptr ->pid);
		if(pid_peer_info_iter !=  _pk_mgr_ptr ->map_pid_peer_info.end() ){
			//��ӥH�W�N�u�βĤ@�Ӫ��s�u
			if(_pk_mgr_ptr ->map_pid_peer_info.count(level_info_ptr ->pid) >= 2 ){
				printf("pid =%d already in connect find in map_pid_peer_info  testing",level_info_ptr ->pid);
				_log_ptr->write_log_format("s =>u s u s\n", __FUNCTION__,__LINE__,"pid =",level_info_ptr ->pid,"already in connect find in map_pid_peer_info testing");
				_log_ptr->write_log_format("s =>u s \n", __FUNCTION__,__LINE__,"fd already in map_pid_peer_info in non_blocking_build_connection (rescue peer)");	
				return 1;
			}
		}

		//�Y�bmap_pid_peerDown_info �h���A���إ߳s�u
		pid_peerDown_info_iter = _pk_mgr_ptr ->map_pid_peerDown_info.find(level_info_ptr ->pid);
		if(pid_peerDown_info_iter != _pk_mgr_ptr ->map_pid_peerDown_info.end()){
			printf("pid =%d already in connect find in map_pid_peerDown_info",level_info_ptr ->pid);
			_log_ptr->write_log_format("s =>u s u s\n", __FUNCTION__,__LINE__,"pid =",level_info_ptr ->pid,"already in connect find in map_pid_peerDown_info");
			_log_ptr->write_log_format("s =>u s \n", __FUNCTION__,__LINE__,"fd already in map_pid_peerdown_info in non_blocking_build_connection (rescue peer)");
			return 1;
		}
	}
	else{
	/*
	this part means that if the child is exist, we don't create it again.
	*/
		multimap<unsigned long, struct peer_info_t *>::iterator pid_peer_info_iter;
		map<unsigned long, int>::iterator map_pid_fd_iter;
		map<unsigned long, struct peer_info_t *>::iterator map_pid_rescue_peer_info_iter;

		//���e�w�g�إ߹L�s�u�� �bmap_out_pid_fd�̭� �h���A�إ�(�O�ҹ�P��child���A�إ߲ĤG���u)
		for(map_pid_fd_iter = _peer_ptr->map_out_pid_fd.begin();map_pid_fd_iter != _peer_ptr->map_out_pid_fd.end(); map_pid_fd_iter++){
			if(map_pid_fd_iter->first == level_info_ptr->pid ){
				_log_ptr->write_log_format("s =>u s \n", __FUNCTION__,__LINE__,"fd already in map_out_pid_fd in non_blocking_build_connection (candidate peer)");
				return 1;
			}
		}

		//�Y�bmap_pid_rescue_peer_info �h���A���إ߳s�u
		map_pid_rescue_peer_info_iter = _pk_mgr_ptr ->map_pid_rescue_peer_info.find(level_info_ptr ->pid);
		if(map_pid_rescue_peer_info_iter != _pk_mgr_ptr ->map_pid_rescue_peer_info.end()){
			printf("pid =%d already in connect find in map_pid_rescue_peer_info",level_info_ptr ->pid);
			_log_ptr->write_log_format("s =>u s u s\n", __FUNCTION__,__LINE__,"pid =",level_info_ptr ->pid,"already in connect find in map_pid_rescue_peer_info");
			_log_ptr->write_log_format("s =>u s \n", __FUNCTION__,__LINE__,"fd already in map_pid_rescue_peer_info in non_blocking_build_connection (candidate peer)");\
			return 1;
		}
	}

	if((_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0 ) {
		cout << "init create socket failure" << endl;

		_net_ptr ->set_nonblocking(_sock);
#ifdef _WIN32
		::WSACleanup();
#endif
		_logger_client_ptr->log_to_server(LOG_WRITE_STRING,0,"s \n","init create socket failure");
		_logger_client_ptr->log_exit();

	}

	map_fd_NonBlockIO_iter=map_fd_NonBlockIO.find(_sock);
	if(map_fd_NonBlockIO_iter != map_fd_NonBlockIO .end()){
		printf("map_fd_NonBlockIO_iter=map_fd_NonBlockIO.find(_sock) error\n ");
		_log_ptr->write_log_format("s =>u s \n", __FUNCTION__,__LINE__,"map_fd_NonBlockIO_iter=map_fd_NonBlockIO.find(_sock) errorn");
		*(_net_ptr->_errorRestartFlag) =RESTART;
	}
	map_fd_NonBlockIO[_sock]=new struct ioNonBlocking;
	memset(map_fd_NonBlockIO[_sock],0x00,sizeof(struct ioNonBlocking));
	map_fd_NonBlockIO[_sock] ->io_nonblockBuff.nonBlockingRecv.recv_packet_state= READ_HEADER_READY ;
	_net_ptr ->set_nonblocking(_sock);	//non-blocking connect
	memset((struct sockaddr_in*)&peer_saddr, 0x0, sizeof(struct sockaddr_in));

    if(flag == 0){	
	    peer_saddr.sin_addr.s_addr = level_info_ptr->public_ip;
		ip.s_addr = level_info_ptr->public_ip;
		printf("connect to public_ip %s port= %d \n" ,inet_ntoa (ip),level_info_ptr->tcp_port );
		_log_ptr->write_log_format("s =>u s u s s s u\n", __FUNCTION__,__LINE__,"connect to PID ",level_info_ptr ->pid,"public_ip",inet_ntoa (ip),"port= ",level_info_ptr->tcp_port );
	}
	else if(flag == 1){	//in the same NAT
		peer_saddr.sin_addr.s_addr = level_info_ptr->private_ip;
		ip.s_addr = level_info_ptr->private_ip;
//		selfip.s_addr = self_public_ip ;
		printf("connect to private_ip %s  port= %d \n", inet_ntoa(ip),level_info_ptr->tcp_port);	
		_log_ptr->write_log_format("s =>u s u s s s u\n", __FUNCTION__,__LINE__,"connect to PID ",level_info_ptr ->pid,"private_ip",inet_ntoa (ip),"port= ",level_info_ptr->tcp_port );
	}
	else{
		
		_logger_client_ptr->log_to_server(LOG_WRITE_STRING,0,"s \n","error : unknown flag in non_blocking_build_connection\n");
		_logger_client_ptr->log_exit();
	}
	peer_saddr.sin_port = htons(level_info_ptr->tcp_port);
	peer_saddr.sin_family = AF_INET;
	
	if(connect(_sock, (struct sockaddr*)&peer_saddr, sizeof(peer_saddr)) < 0) {
		if(WSAGetLastError() == WSAEWOULDBLOCK){

			_net_ptr->set_nonblocking(_sock);
			_net_ptr->epoll_control(_sock, EPOLL_CTL_ADD, EPOLLIN | EPOLLOUT);
			_net_ptr->set_fd_bcptr_map(_sock, dynamic_cast<basic_class *> (_io_connect_ptr));
			_peer_mgr_ptr->fd_list_ptr->push_back(_sock);	
			_log_ptr->write_log_format("s =>u s \n", __FUNCTION__,__LINE__,"build_ connection failure : WSAEWOULDBLOCK");
		}
		else{
			
	#ifdef _WIN32
			::closesocket(_sock);
			::WSACleanup();
			
			_logger_client_ptr->log_to_server(LOG_WRITE_STRING,0,"s d \n","build_ connection failure : ",WSAGetLastError());
			_logger_client_ptr->log_exit();
	#else
			::close(_sock);
	#endif
		}

	} else {
		
		_logger_client_ptr->log_to_server(LOG_WRITE_STRING,0,"s \n","build_ connection cannot too fast ");
		_logger_client_ptr->log_exit();
		/*
		_net_ptr->set_nonblocking(_sock);
		_net_ptr->epoll_control(_sock, EPOLL_CTL_ADD, EPOLLIN | EPOLLOUT);	
		_net_ptr->set_fd_bcptr_map(_sock, dynamic_cast<basic_class *>(this));*/
	}

	/*
	this part stores the info in each table.
	*/
	_log_ptr->write_log_format("s =>u s d s d s d s d s d s\n", __FUNCTION__,__LINE__,"non blocking connect (before) fd : ",_sock," manifest : ",manifest," session_id : ",session_id," role : ",fd_role," pid : ",fd_pid," non_blocking_build_connection (candidate peer)\n");
	
	map_fd_info_iter = map_fd_info.find(_sock);
	if(map_fd_info_iter != map_fd_info.end()){
		
		_logger_client_ptr->log_to_server(LOG_WRITE_STRING,0,"s d \n","error : fd %d already in map_fd_info in non_blocking_build_connection\n",_sock);
		_logger_client_ptr->log_exit();
	}
	map_fd_info[_sock] = new struct fd_information;

	map_fd_info_iter = map_fd_info.find(_sock);
	if(map_fd_info_iter == map_fd_info.end()){
		
		_logger_client_ptr->log_to_server(LOG_WRITE_STRING,0,"s d \n","error : %d cannot new non_blocking_build_connection\n",_sock);
		_logger_client_ptr->log_exit();
	}

	memset(map_fd_info_iter->second,0x00,sizeof(struct fd_information));
	map_fd_info_iter->second->flag = fd_role;
	map_fd_info_iter->second->manifest = manifest;
	map_fd_info_iter->second->pid = fd_pid;
	map_fd_info_iter->second->session_id = session_id;
	
	_log_ptr->write_log_format("s =>u s d s d s d s d s d s\n", __FUNCTION__,__LINE__,"non blocking connect fd : ",map_fd_info_iter->first," manifest : ",map_fd_info_iter->second->manifest," session_id : ",map_fd_info_iter->second->session_id," role : ",map_fd_info_iter->second->flag," pid : ",map_fd_info_iter->second->pid," non_blocking_build_connection (candidate peer)\n");
	

	return RET_OK;
}

io_accept * peer_communication::get_io_accept_handler(){
	return _io_accept_ptr;
}

void peer_communication::fd_close(int sock){
	_log_ptr->write_log_format("s =>u s d\n", __FUNCTION__,__LINE__,"close in peer_communication::fd_close fd ",sock);
	_net_ptr->close(sock);

	list<int>::iterator fd_iter;
	for(fd_iter = _peer_ptr->fd_list_ptr->begin(); fd_iter != _peer_ptr->fd_list_ptr->end(); fd_iter++) {
		if(*fd_iter == sock) {
			_peer_ptr->fd_list_ptr->erase(fd_iter);
			break;
		}
	}

	map_fd_info_iter = map_fd_info.find(sock);
	if(map_fd_info_iter == map_fd_info.end()){
		//_log_ptr->write_log_format("s =>u s d\n", __FUNCTION__,__LINE__,"error : close cannot find table in peer_communication::fd_close fd ",sock);
		//_logger_client_ptr->log_to_server(LOG_WRITE_STRING,0,"s d \n","error : close cannot find table in peer_communication::fd_close fd ",sock);
		//_logger_client_ptr->log_exit();
		
	}
	else{
		delete map_fd_info_iter->second;
		map_fd_info.erase(sock);
	}

	map_fd_NonBlockIO_iter= map_fd_NonBlockIO.find(sock);
	if(map_fd_NonBlockIO_iter != map_fd_NonBlockIO.end()){
		delete map_fd_NonBlockIO_iter->second;
		map_fd_NonBlockIO.erase(map_fd_NonBlockIO_iter);
	
	}



	list<int>::iterator map_fd_unknown_iter;

	for(map_fd_unknown_iter = _io_accept_ptr->map_fd_unknown.begin();map_fd_unknown_iter != _io_accept_ptr->map_fd_unknown.end();map_fd_unknown_iter++){
		if( sock == *map_fd_unknown_iter){
			_io_accept_ptr->map_fd_unknown.erase(map_fd_unknown_iter);
			break;
		}
	}
			


}

void peer_communication::stop_attempt_connect(unsigned long stop_session_id){
	/*
	erase the manifest structure, and close and take out the fd if it is not in fd_pid table.
	*/
	_log_ptr->write_log_format("s =>u \n", __FUNCTION__,__LINE__);
	session_id_candidates_set_iter = session_id_candidates_set.find(stop_session_id);
	if(session_id_candidates_set_iter == session_id_candidates_set.end()){
		_log_ptr->write_log_format("s =>u s \n", __FUNCTION__,__LINE__,"cannot find stop_session_id in structure in stop_attempt_connect");
		_logger_client_ptr->log_to_server(LOG_WRITE_STRING,0,"s \n","cannot find stop_session_id in structure in stop_attempt_connect\n");
		_logger_client_ptr->log_exit();
	}
	else{
		int delete_fd_flag = 0;

		if(session_id_candidates_set_iter->second->role == 0){	//caller is rescue peer
			total_manifest = total_manifest & (~session_id_candidates_set_iter->second->manifest);
			_log_ptr->write_log_format("s =>u s \n", __FUNCTION__,__LINE__,"find candidates in structure in stop_attempt_connect may be rescue peer");
			_log_ptr->write_log_format("s =>u s d s d s d s d\n", __FUNCTION__,__LINE__,"session_id : ",stop_session_id,", manifest : ",session_id_candidates_set_iter->second->manifest,", role: ",session_id_candidates_set_iter->second->role,", list_number: ",session_id_candidates_set_iter->second->peer_num);
			for(int i=0;i<session_id_candidates_set_iter->second->peer_num;i++){
				//fprintf(peer_com_log,"list pid : %d, public_ip : %s, private_ip: %s\n",session_id_candidates_set_iter->second->list_info->level_info[i]->pid,session_id_candidates_set_iter->second->list_info->level_info[i]->public_ip,session_id_candidates_set_iter->second->list_info->level_info[i]->private_ip);
				_log_ptr->write_log_format("s =>u s d\n", __FUNCTION__,__LINE__,"list pid : ",session_id_candidates_set_iter->second->list_info->level_info[i]->pid);
				delete session_id_candidates_set_iter->second->list_info->level_info[i];

			}

			delete session_id_candidates_set_iter->second->list_info;
			delete session_id_candidates_set_iter->second;
			session_id_candidates_set.erase(session_id_candidates_set_iter);

			/*map<int, unsigned long>::iterator map_fd_session_id_iter;
			map<int, unsigned long>::iterator map_peer_com_fd_pid_iter;
			map<int, unsigned long>::iterator map_fd_manifest_iter;*/

			map_fd_info_iter = map_fd_info.begin();
			while(map_fd_info_iter != map_fd_info.end()){
				if(map_fd_info_iter->second->session_id == stop_session_id){

					delete_fd_flag = 1;

					if(_peer_ptr->map_fd_pid.find(map_fd_info_iter->first) == _peer_ptr->map_fd_pid.end()){
						/*
						connect faild delete table and close fd
						*/


						_log_ptr->write_log_format("s =>u s d\n", __FUNCTION__,__LINE__,"connect faild delete table and close fd ",map_fd_info_iter->first);
						/*
						close fd
						*/
						list<int>::iterator fd_iter;
	
						_log_ptr->write_log_format("s => s \n", (char*)__PRETTY_FUNCTION__, "peer_com");
						cout << "peer_com close fd since timeout " << map_fd_info_iter->first <<  endl;
//						_net_ptr->epoll_control(map_fd_info_iter->first, EPOLL_CTL_DEL, 0);
						_net_ptr->close(map_fd_info_iter->first);

						for(fd_iter = _peer_mgr_ptr->fd_list_ptr->begin(); fd_iter != _peer_mgr_ptr->fd_list_ptr->end(); fd_iter++) {
							if(*fd_iter == map_fd_info_iter->first) {
								_peer_mgr_ptr->fd_list_ptr->erase(fd_iter);
								break;
							}
						}
					}
					else{
						/*
						connect succeed just delete table
						*/
						_log_ptr->write_log_format("s =>u s \n", __FUNCTION__,__LINE__,"connect succeed just delete table");
						
					}
					
					map_fd_NonBlockIO_iter= map_fd_NonBlockIO.find(map_fd_info_iter->first);
					if(map_fd_NonBlockIO_iter != map_fd_NonBlockIO.end()){
						delete map_fd_NonBlockIO_iter->second;
						map_fd_NonBlockIO.erase(map_fd_NonBlockIO_iter);

					}


					delete [] map_fd_info_iter ->second;
					map_fd_info.erase(map_fd_info_iter);
					map_fd_info_iter = map_fd_info.begin();
				}
				else{
					map_fd_info_iter++;
				}
			}
		}
		else{	//caller is candidate
			_log_ptr->write_log_format("s =>u s \n", __FUNCTION__,__LINE__,"find candidates in structure in stop_attempt_connect may be candidate peer");
			_log_ptr->write_log_format("s =>u s d s d s d s d\n", __FUNCTION__,__LINE__,"session_id : ",stop_session_id,", manifest : ",session_id_candidates_set_iter->second->manifest,", role: ",session_id_candidates_set_iter->second->role,", list_number: ",session_id_candidates_set_iter->second->peer_num);
			for(int i=0;i<session_id_candidates_set_iter->second->peer_num;i++){
				//fprintf(peer_com_log,"list pid : %d, public_ip : %s, private_ip: %s\n",session_id_candidates_set_iter->second->list_info->level_info[i]->pid,session_id_candidates_set_iter->second->list_info->level_info[i]->public_ip,session_id_candidates_set_iter->second->list_info->level_info[i]->private_ip);
				_log_ptr->write_log_format("s =>u s d \n", __FUNCTION__,__LINE__,"list pid : ",session_id_candidates_set_iter->second->list_info->level_info[i]->pid);
				delete session_id_candidates_set_iter->second->list_info->level_info[i];

			}

			delete session_id_candidates_set_iter->second->list_info;
			delete session_id_candidates_set_iter->second;
			session_id_candidates_set.erase(session_id_candidates_set_iter);

			/*map<int, unsigned long>::iterator map_fd_session_id_iter;
			map<int, unsigned long>::iterator map_peer_com_fd_pid_iter;
			map<int, unsigned long>::iterator map_fd_manifest_iter;*/

			map_fd_info_iter = map_fd_info.begin();
			while(map_fd_info_iter != map_fd_info.end()){
				if(map_fd_info_iter->second->session_id == stop_session_id){

					delete_fd_flag = 1;

					if(_peer_ptr->map_fd_pid.find(map_fd_info_iter->first) == _peer_ptr->map_fd_pid.end()){
						/*
						connect faild delete table and close fd
						*/
						

						_log_ptr->write_log_format("s =>u s d \n", __FUNCTION__,__LINE__,"connect faild delete table and close fd ",map_fd_info_iter->first);
						/*
						close fd
						*/
						list<int>::iterator fd_iter;
	
						_log_ptr->write_log_format("s => s \n", (char*)__PRETTY_FUNCTION__, "peer_com");
						cout << "peer_com close fd since timeout " << map_fd_info_iter->first <<  endl;
//						_net_ptr->epoll_control(map_fd_info_iter->first, EPOLL_CTL_DEL, 0);
						_net_ptr->close(map_fd_info_iter->first);

						for(fd_iter = _peer_mgr_ptr->fd_list_ptr->begin(); fd_iter != _peer_mgr_ptr->fd_list_ptr->end(); fd_iter++) {
							if(*fd_iter == map_fd_info_iter->first) {
								_peer_mgr_ptr->fd_list_ptr->erase(fd_iter);
								break;
							}
						}
					}
					else{
						/*
						connect succeed just delete table
						*/
						_log_ptr->write_log_format("s =>u s \n", __FUNCTION__,__LINE__,"connect succeed just delete table ");
						

					}

					map_fd_NonBlockIO_iter= map_fd_NonBlockIO.find(map_fd_info_iter->first);
					if(map_fd_NonBlockIO_iter != map_fd_NonBlockIO.end()){
						delete map_fd_NonBlockIO_iter->second;
						map_fd_NonBlockIO.erase(map_fd_NonBlockIO_iter);

					}



					delete map_fd_info_iter->second;
					map_fd_info.erase(map_fd_info_iter);
					map_fd_info_iter = map_fd_info.begin();
				}
				else{
					map_fd_info_iter++;
				}
			}
		}

		//map<int, unsigned long>::iterator map_peer_com_fd_pid_iter;
		if(delete_fd_flag==0){
			_log_ptr->write_log_format("s =>u s \n", __FUNCTION__,__LINE__,"cannot find fd info table ");
		}
		else{
			_log_ptr->write_log_format("s =>u s \n", __FUNCTION__,__LINE__,"delete fd info table");
		}
		_log_ptr->write_log_format("s =>u s \n", __FUNCTION__,__LINE__,"io handler : ");
		for(map_fd_info_iter = map_fd_info.begin();map_fd_info_iter != map_fd_info.end();map_fd_info_iter++){
			_log_ptr->write_log_format("s =>u s d s d \n", __FUNCTION__,__LINE__,"fd : ",map_fd_info_iter->first,", pid: ",map_fd_info_iter->second->pid);
		}
		_log_ptr->write_log_format("s =>u \n", __FUNCTION__,__LINE__);
		_log_ptr->write_log_format("s =>u \n", __FUNCTION__,__LINE__);
		_log_ptr->write_log_format("s =>u \n", __FUNCTION__,__LINE__);
	}
}



int peer_communication::handle_pkt_in(int sock)
{	
	/*
	this part shows that the peer may connect to others (connect) or be connected by others (accept)
	it will only receive PEER_CON protocol sent by join/rescue peer (the peer is candidate's peer).
	And handle P2P structure.
	*/
	map_fd_info_iter = map_fd_info.find(sock);
	if(map_fd_info_iter == map_fd_info.end()){
		
		_logger_client_ptr->log_to_server(LOG_WRITE_STRING,0,"s \n","cannot find map_fd_info structure in peer_communication::handle_pkt_in\n");
		_logger_client_ptr->log_exit();
	}
	else{
		if(map_fd_info_iter->second->flag == 0){	//this fd is rescue peer
			//do nothing rebind to event out only
			_net_ptr->set_nonblocking(sock);
			_net_ptr->epoll_control(sock, EPOLL_CTL_MOD, EPOLLOUT);
			_log_ptr->write_log_format("s =>u s \n", __FUNCTION__,__LINE__,"this fd is rescue peer do nothing rebind to event out only");
		}
		else if(map_fd_info_iter->second->flag == 1){	//this fd is candidate
			/*
			read peer con
			*/
			//int recv_byte;	
			//int expect_len;
			//int offset = 0;
			//unsigned long buf_len;
//			struct chunk_t *chunk_ptr = NULL;
//			struct chunk_header_t *chunk_header_ptr = NULL;
	
//			chunk_header_ptr = new struct chunk_header_t;
//			memset(chunk_header_ptr, 0x0, sizeof(struct chunk_header_t));
//			expect_len = sizeof(struct chunk_header_t) ;

/*
			while (1) {
				recv_byte = recv(sock, (char *)chunk_header_ptr + offset, expect_len, 0);
				if (recv_byte < 0) {
		#ifdef _WIN32 
					if (WSAGetLastError() == WSAEWOULDBLOCK) {
		#else
					if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
		#endif
						continue;
					} else {
						DBG_PRINTF("here\n");
						//continue;
						_logger_client_ptr->log_to_server(LOG_WRITE_STRING,0,"s d \n","error in peer_communication::handle_pkt_in (recv -1) error number : ",WSAGetLastError());
						_logger_client_ptr->log_exit();
						return RET_SOCK_ERROR;
						//PAUSE
						//_log_ptr->exit(0, "recv error in peer_mgr::handle_pkt_in");
					}
			
				}
				else if(recv_byte == 0){
					printf("sock closed\n");
					cout << "error in peer_communication::handle_pkt_in (recv 0) error number : "<<WSAGetLastError()<< endl;
					_log_ptr->write_log_format("s =>u s d \n", __FUNCTION__,__LINE__,"error in peer_communication::handle_pkt_in (recv 0) error number : ",WSAGetLastError());
					fd_close(sock);
					//PAUSE
					//exit(1);
						//PAUSE
					return RET_SOCK_ERROR;
				}
				expect_len -= recv_byte;
				offset += recv_byte;
		
				if (!expect_len)
					break;
			}

			expect_len = chunk_header_ptr->length;
	
			buf_len = sizeof(struct chunk_header_t) + expect_len;
			cout << "buf_len = " << buf_len << endl;

			chunk_ptr = (struct chunk_t *)new unsigned char[buf_len];

			if (!chunk_ptr) {
				
				_logger_client_ptr->log_to_server(LOG_WRITE_STRING,0,"s d \n","error in peer_communication::handle_pkt_in (new chunk failed) error number : ",WSAGetLastError());
				_logger_client_ptr->log_exit();
				_log_ptr->exit(0, "memory not enough");
				return RET_SOCK_ERROR;
			}

			memset(chunk_ptr, 0x0, buf_len);
		
			memcpy(chunk_ptr, chunk_header_ptr, sizeof(struct chunk_header_t));

			if(chunk_header_ptr)
				delete chunk_header_ptr;
	
			while (1) {
				recv_byte = recv(sock, (char *)chunk_ptr + offset, expect_len, 0);
				if (recv_byte < 0) {
		#ifdef _WIN32 
					if (WSAGetLastError() == WSAEWOULDBLOCK) {
		#else
					if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
		#endif
						continue;
					} else {
						
						_logger_client_ptr->log_to_server(LOG_WRITE_STRING,0,"s d \n","error in peer_communication::handle_pkt_in (recv -1 payload) error number : ",WSAGetLastError());
						_logger_client_ptr->log_exit();
						cout << "haha5" << endl;
						//PAUSE
						return RET_SOCK_ERROR;
						//_log_ptr->exit(0, "recv error in peer_mgr::handle_pkt_in");
					}
				}
				else if(recv_byte == 0){
					printf("sock closed\n");
					cout << "error in peer_communication::handle_pkt_in (recv 0 payload) error number : "<<WSAGetLastError()<< endl;
					_log_ptr->write_log_format("s =>u s \n", __FUNCTION__,__LINE__,"error in peer_communication::handle_pkt_in (recv 0) error number : ",WSAGetLastError());
					fd_close(sock);
					//PAUSE
					//exit(1);
						//PAUSE
					return RET_SOCK_ERROR;
				}
				expect_len -= recv_byte;
				offset += recv_byte;
				if (expect_len == 0)
					break;
			}
*/

			map_fd_NonBlockIO_iter =map_fd_NonBlockIO.find(sock);
			if(map_fd_NonBlockIO_iter==map_fd_NonBlockIO.end()){
				printf("can't  find map_fd_NonBlockIO_iter in peer_commiication");
				_log_ptr->write_log_format("s =>u s \n", __FUNCTION__,__LINE__,"can't  find map_fd_NonBlockIO_iter in peer_commiication");
				*(_net_ptr->_errorRestartFlag) =RESTART;
			}

			Nonblocking_Ctl * Nonblocking_Recv_Ctl_ptr =NULL;
			struct chunk_header_t* chunk_header_ptr = NULL;
			struct chunk_t* chunk_ptr = NULL;
			unsigned long buf_len=0;
			int recv_byte=0;

			Nonblocking_Recv_Ctl_ptr = &(map_fd_NonBlockIO_iter->second->io_nonblockBuff.nonBlockingRecv) ;


			for(int i =0;i<5;i++){


				if(Nonblocking_Recv_Ctl_ptr->recv_packet_state == READ_HEADER_READY){


					chunk_header_ptr = (struct chunk_header_t *)new unsigned char[sizeof(chunk_header_t)];
					memset(chunk_header_ptr, 0x0, sizeof(struct chunk_header_t));

					Nonblocking_Recv_Ctl_ptr ->recv_ctl_info.offset =0 ;
					Nonblocking_Recv_Ctl_ptr ->recv_ctl_info.total_len = sizeof(chunk_header_t) ;
					Nonblocking_Recv_Ctl_ptr ->recv_ctl_info.expect_len = sizeof(chunk_header_t) ;
					Nonblocking_Recv_Ctl_ptr ->recv_ctl_info.buffer = (char *)chunk_header_ptr ;


				}else if(Nonblocking_Recv_Ctl_ptr->recv_packet_state == READ_HEADER_RUNNING){

					//do nothing

				}else if(Nonblocking_Recv_Ctl_ptr->recv_packet_state == READ_HEADER_OK){

					buf_len = sizeof(chunk_header_t)+ ((chunk_t *)(Nonblocking_Recv_Ctl_ptr->recv_ctl_info.buffer)) ->header.length ;
					chunk_ptr = (struct chunk_t *)new unsigned char[buf_len];

					//			printf("buf_len %d \n",buf_len);

					memset(chunk_ptr, 0x0, buf_len);

					memcpy(chunk_ptr,Nonblocking_Recv_Ctl_ptr->recv_ctl_info.buffer,sizeof(chunk_header_t));

					if (Nonblocking_Recv_Ctl_ptr->recv_ctl_info.buffer)
						delete [] (unsigned char*)Nonblocking_Recv_Ctl_ptr->recv_ctl_info.buffer ;

					Nonblocking_Recv_Ctl_ptr ->recv_ctl_info.offset =sizeof(chunk_header_t) ;
					Nonblocking_Recv_Ctl_ptr ->recv_ctl_info.total_len = chunk_ptr->header.length ;
					Nonblocking_Recv_Ctl_ptr ->recv_ctl_info.expect_len = chunk_ptr->header.length ;
					Nonblocking_Recv_Ctl_ptr ->recv_ctl_info.buffer = (char *)chunk_ptr ;

					//			printf("chunk_ptr->header.length = %d  seq = %d\n",chunk_ptr->header.length,chunk_ptr->header.sequence_number);
					Nonblocking_Recv_Ctl_ptr->recv_packet_state = READ_PAYLOAD_READY ;

				}else if(Nonblocking_Recv_Ctl_ptr->recv_packet_state == READ_PAYLOAD_READY){

					//do nothing

				}else if(Nonblocking_Recv_Ctl_ptr->recv_packet_state == READ_PAYLOAD_RUNNING){

					//do nothing

				}else if(Nonblocking_Recv_Ctl_ptr->recv_packet_state == READ_PAYLOAD_OK){

					//			chunk_ptr =(chunk_t *)Recv_nonblocking_ctl_ptr ->recv_ctl_info.buffer;

					//			Recv_nonblocking_ctl_ptr->recv_packet_state = READ_HEADER_READY ;

					break;

				}


				recv_byte =_net_ptr->nonblock_recv(sock,Nonblocking_Recv_Ctl_ptr);


				if(recv_byte < 0) {
					printf("error occ in nonblocking \n");
					fd_close(sock);

					//PAUSE
					return RET_SOCK_ERROR;
				}


			}




			if(Nonblocking_Recv_Ctl_ptr->recv_packet_state == READ_PAYLOAD_OK){

				chunk_ptr =(chunk_t *)Nonblocking_Recv_Ctl_ptr ->recv_ctl_info.buffer;

				Nonblocking_Recv_Ctl_ptr->recv_packet_state = READ_HEADER_READY ;

				buf_len =  sizeof(struct chunk_header_t) +  chunk_ptr->header.length ;

			}else{

				//other stats
				return RET_OK;

			}




			if (chunk_ptr->header.cmd == CHNK_CMD_PEER_CON) {
				cout << "CHNK_CMD_PEER_CON" << endl;
				_log_ptr->write_log_format("s =>u s \n", __FUNCTION__,__LINE__,"CHNK_CMD_PEER_CON ");
				_log_ptr->write_log_format("s =>u s \n", __FUNCTION__,__LINE__,"CHNK_CMD_PEER_CON");
				/*
				use fake cin info
				*/
				struct sockaddr_in fake_cin;
				memset(&fake_cin,0x00,sizeof(struct sockaddr_in));

				_peer_ptr->handle_connect(sock, chunk_ptr,fake_cin);

				/*
				bind to peer_com~ object
				*/
				_net_ptr->set_nonblocking(sock);
				_net_ptr->epoll_control(sock, EPOLL_CTL_MOD, EPOLLIN | EPOLLOUT);
				_net_ptr->set_fd_bcptr_map(sock, dynamic_cast<basic_class *> (_peer_ptr));
			} else{
				
				printf("error : unknow or cannot handle cmd : in peer_communication::handle_pkt_in  cmd =%d \n",chunk_ptr->header.cmd);
				_logger_client_ptr->log_to_server(LOG_WRITE_STRING,0,"s d \n","error : unknow or cannot handle cmd : in peer_communication::handle_pkt_in ",chunk_ptr->header.cmd);
				_logger_client_ptr->log_exit();
			}

			if(chunk_ptr)
				delete chunk_ptr;
		}
		else{
		
			_logger_client_ptr->log_to_server(LOG_WRITE_STRING,0,"s \n","error : unknow flag in peer_communication::handle_pkt_in\n");
			_logger_client_ptr->log_exit();
		}
	}


	return RET_OK;
}

int peer_communication::handle_pkt_out(int sock)	//first write, then set fd to readable & excecption only
{
	/*
	this part shows that the peer may connect to others (connect) or be connected by others (accept)
	it will only send PEER_CON protocol to candidates, if the fd is in the list. (the peer is join/rescue peer)
	And handle P2P structure.
	*/
	map_fd_info_iter = map_fd_info.find(sock);
	if(map_fd_info_iter == map_fd_info.end()){
		
		_logger_client_ptr->log_to_server(LOG_WRITE_STRING,0,"s \n","cannot find map_fd_info_iter structure in peer_communication::handle_pkt_out\n");
		_logger_client_ptr->log_exit();
	}
	else{
		if(map_fd_info_iter->second->flag == 0){	//this fd is rescue peer
			//send peer con
			int ret,send_flag;
			int i;
			/*map<int, unsigned long>::iterator map_fd_session_id_iter;
			map<int, unsigned long>::iterator map_peer_com_fd_pid_iter;
			map<int, unsigned long>::iterator map_fd_manifest_iter;*/

			send_flag=0;
			

			session_id_candidates_set_iter = session_id_candidates_set.find(map_fd_info_iter->second->session_id);
			if(session_id_candidates_set_iter == session_id_candidates_set.end()){
				
				_logger_client_ptr->log_to_server(LOG_WRITE_STRING,0,"s \n","cannot find session_id_candidates_set structure in peer_communication::handle_pkt_out\n");
				_logger_client_ptr->log_exit();
			}

			

			/*
			this part use to check, if the cnnection is already exist.
			*/
			multimap<unsigned long, struct peer_info_t *>::iterator pid_peer_info_iter;
			map<unsigned long, int>::iterator map_pid_fd_iter;
			map<unsigned long, struct peer_connect_down_t *>::iterator pid_peerDown_info_iter;
			int check_flag = 0;

			/*
			//���e�w�g�إ߹L�s�u�� �bmap_in_pid_fd�̭� �h���A�إ�(�O�ҹ�P��parent���A�إ߲ĤG���u)
			for(map_pid_fd_iter = _peer_ptr->map_in_pid_fd.begin();map_pid_fd_iter != _peer_ptr->map_in_pid_fd.end(); map_pid_fd_iter++){
				if(map_pid_fd_iter->first == map_fd_info_iter->second->pid ){
					_log_ptr->write_log_format("s =>u s \n", __FUNCTION__,__LINE__,"fd already in map_in_pid_fd in peer_communication::handle_pkt_out (rescue peer)");
					check_flag = 1;
				}
			}

			
			pid_peer_info_iter = _pk_mgr_ptr ->map_pid_peer_info.find(map_fd_info_iter->second->pid);
			if(pid_peer_info_iter !=  _pk_mgr_ptr ->map_pid_peer_info.end() ){
				//��ӥH�W�N�u�βĤ@�Ӫ��s�u
				if(_pk_mgr_ptr ->map_pid_peer_info.count(map_fd_info_iter->second->pid) >= 2 ){
					printf("pid =%d already in connect find in map_pid_peer_info  testing in peer_communication::handle_pkt_out\n",map_fd_info_iter->second->pid);
					_log_ptr->write_log_format("s =>u s u s\n", __FUNCTION__,__LINE__,"pid =",map_fd_info_iter->second->pid,"already in connect find in map_pid_peer_info testing in peer_communication::handle_pkt_out");
					_log_ptr->write_log_format("s =>u s \n", __FUNCTION__,__LINE__,"fd already in map_pid_peer_info in peer_communication::handle_pkt_out (rescue peer)");	
					check_flag = 1;
				}
			}

			//�Y�bmap_pid_peerDown_info �h���A���إ߳s�u
			pid_peerDown_info_iter = _pk_mgr_ptr ->map_pid_peerDown_info.find(map_fd_info_iter->second->pid);
			if(pid_peerDown_info_iter != _pk_mgr_ptr ->map_pid_peerDown_info.end()){
				printf("pid =%d already in connect find in map_pid_peerDown_info in peer_communication::handle_pkt_out\n",map_fd_info_iter->second->pid);
				_log_ptr->write_log_format("s =>u s u s\n", __FUNCTION__,__LINE__,"pid =",map_fd_info_iter->second->pid,"already in connect find in map_pid_peerDown_info in peer_communication::handle_pkt_out");
				_log_ptr->write_log_format("s =>u s \n", __FUNCTION__,__LINE__,"fd already in map_pid_peerdown_info in peer_communication::handle_pkt_out (rescue peer)");
				check_flag = 1;
			}*/

			if(check_flag == 1){
				_log_ptr->write_log_format("s =>u s \n", __FUNCTION__,__LINE__,"we won't send CHNK_CMD_PEER_CON, since the connection is already exist");
				_net_ptr->set_nonblocking(sock);
				_net_ptr->epoll_control(sock, EPOLL_CTL_MOD, EPOLLIN | EPOLLOUT);	
				_net_ptr->set_fd_bcptr_map(sock, dynamic_cast<basic_class *> (_peer_ptr));
				return RET_OK;
			}

//			_net_ptr->set_blocking(sock);

			map_fd_NonBlockIO_iter =map_fd_NonBlockIO.find(sock);
			if(map_fd_NonBlockIO_iter==map_fd_NonBlockIO.end()){
				printf("can't  find map_fd_NonBlockIO_iter in peer_commiication");
				_log_ptr->write_log_format("s =>u s \n", __FUNCTION__,__LINE__,"can't  find map_fd_NonBlockIO_iter in peer_commiication");
				*(_net_ptr->_errorRestartFlag) =RESTART;
			}

			for(i=0;i<session_id_candidates_set_iter->second->peer_num;i++){
				if(session_id_candidates_set_iter->second->list_info->level_info[i]->pid == map_fd_info_iter->second->pid){
					ret = _peer_ptr->handle_connect_request(sock, session_id_candidates_set_iter->second->list_info->level_info[i], session_id_candidates_set_iter->second->list_info->pid,&(map_fd_NonBlockIO_iter->second->io_nonblockBuff.nonBlockingSendCtrl));
					send_flag =1;
				}
			}

			if(send_flag == 0){
				
				_logger_client_ptr->log_to_server(LOG_WRITE_STRING,0,"s \n","cannot find level info structure in peer_communication::handle_pkt_out\n");
				_logger_client_ptr->log_exit();
			}

			_log_ptr->write_log_format("s =>u s \n", __FUNCTION__,__LINE__,"send CHNK_CMD_PEER_CON");
			if(ret < 0) {
				cout << "handle_connect_request error!!!" << endl;
				fd_close(sock);
				return RET_ERROR;
			} else if(map_fd_NonBlockIO_iter ->second->io_nonblockBuff.nonBlockingSendCtrl.recv_packet_state == RUNNING){
			
			
			_net_ptr->epoll_control(sock, EPOLL_CTL_MOD, EPOLLIN | EPOLLOUT);	
			
			
			
			}else if (map_fd_NonBlockIO_iter ->second->io_nonblockBuff.nonBlockingSendCtrl.recv_packet_state == READY){
				cout << "sock = " << sock << endl;

				_net_ptr->set_nonblocking(sock);
				_net_ptr->epoll_control(sock, EPOLL_CTL_MOD, EPOLLIN | EPOLLOUT);	
				_net_ptr->set_fd_bcptr_map(sock, dynamic_cast<basic_class *> (_peer_ptr));
				return RET_OK;
			}
		}
		else if(map_fd_info_iter->second->flag == 1){	//this fd is candidate
			//do nothing rebind to event in only
			_net_ptr->set_nonblocking(sock);
			_log_ptr->write_log_format("s =>u s \n", __FUNCTION__,__LINE__,"this fd is candidate do nothing rebind to event in only");
			_net_ptr->epoll_control(sock, EPOLL_CTL_MOD, EPOLLIN);
		}
		else{
			
			_logger_client_ptr->log_to_server(LOG_WRITE_STRING,0,"s \n","error : unknow flag in peer_communication::handle_pkt_out\n");
			_logger_client_ptr->log_exit();
		}
	}

	return RET_OK;
}

void peer_communication::handle_pkt_error(int sock)
{
	
	_logger_client_ptr->log_to_server(LOG_WRITE_STRING,0,"s d \n","error in peer_communication error number : ",WSAGetLastError());
	_logger_client_ptr->log_exit();
}

void peer_communication::handle_job_realtime()
{

}


void peer_communication::handle_job_timer()
{

}

void peer_communication::handle_sock_error(int sock, basic_class *bcptr){
}