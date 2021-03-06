#include"ftpServer.h"
#define AUTH ".auth"

ftpServer::ftpServer(int sock)
	:sock_ctl(sock)
{}

ftpServer::~ftpServer()
{}

void ftpServer::process()
{
	ftpserver_process();
	close(sock_ctl);
}

void ftpServer::ftpserver_process()
{
	send_response(sock_ctl,220);   //发送接受处理响应码

	if(1==ftpserver_login())    //登录成功
	{
		send_response(sock_ctl,230);  
	}
	else                         //登录失败，结束本次会话
	{
		send_response(sock_ctl,430);  
		return;
	}

	char cmd[5];
	char arg[MAXSIZE];
	while(1)
	{
		//接收命令，解析，获取命令和参数
		int status=ftpserver_recv_cmd(cmd,arg);
		if((status<0)||(221==status))   //出错或者时QUIT
			break;
			
		if(200==status)     //处理
		{
			int sock_data=ftpserver_start_data_conn();
			if(sock_data<0)
			{
				//print_log()
				return;
			}
			
			//执行命令
			if(strcmp(cmd,"LIST")==0)
			{
				ftpserver_list(sock_data);
			}
			else if(strcmp(cmd,"RETR")==0)
			{
				ftpserver_retr(sock_data,arg);
			}
			else if(strcmp(cmd,"PUSH")==0)    
			{
				ftpserver_push(sock_data,arg);
			}
			close(sock_data);
		}
	}
}


int ftpServer::ftpserver_recv_cmd(char* cmd,char* arg)
{
	int status=200;
	char buf[MAXSIZE];

	memset(buf,0,MAXSIZE);
	memset(cmd,0,5);
	memset(arg,0,MAXSIZE);
	if(-1==recv_data(sock_ctl,buf,MAXSIZE))
	{
		//print_log()
		return -1;
	}
	
	strncpy(cmd,buf,4);
	strcpy(arg,buf+5);

	if(strcmp(cmd,"QUIT")==0)
		status=221;
	else if((strcmp(cmd,"USER")==0)||(strcmp(cmd,"PASS")==0)||\
			(strcmp(cmd,"LIST")==0)||(strcmp(cmd,"RETR")==0)||(strcmp(cmd,"PUSH")==0))
		status=200;
	else
		status=502;

	send_response(sock_ctl,status);   //发送状态
	return status;
}


int ftpServer::ftpserver_login()
{

	char buf[MAXSIZE];
	char user[MAXSIZE];
	char pass[MAXSIZE];
	memset(buf,0,MAXSIZE);
	memset(user,0,MAXSIZE);
	memset(pass,0,MAXSIZE);

	if(recv_data(sock_ctl,buf,sizeof(buf))<0)
	{
		//print_log()
		return -1;
	}

	int i=5;    //buf[0-4] "Name "
	int j=0;
	while(buf[i]!=0)       //将用户名保存起来
		user[j++]=buf[i++];
	
	send_response(sock_ctl,331);    //通知用户输入密码

	memset(buf,0,MAXSIZE);
	if(recv_data(sock_ctl,buf,sizeof(buf))<0)  //获取密码
	{
		//print_log()
		return -1;
	}

	i=5;    //buf[0-4] "pass "
	j=0;
	while(buf[i]!=0)       //将密码保存起来
		pass[j++]=buf[i++];
	int ret=ftpserver_check_user(user,pass);    //验证用户名和密码
	return ret;
}


int ftpServer::ftpserver_check_user(const char* user,const char* pass )
{
	FILE* fd=fopen(AUTH,"r");
	if(NULL==fd)
	{
		//print_log()
		return -1;
	}

	char username[MAXSIZE];
	char password[MAXSIZE];
	char buf[MAXSIZE];
	char* line=NULL;
	size_t len=0;
	int auth=-1;
	while(-1!=getline(&line,&len,fd))
	{
		memset(buf,0,MAXSIZE);
		strcpy(buf,line);          
		
		char *post=strtok(buf," ");    //分割出用户名 
		strcpy(username,post);

		if(NULL!=post)
		{
			char *post=strtok(NULL," ");     //分割出密码
			strcpy(password,post);
		}

		trimstr(password,(int)strlen(password)); //去掉字符串中放入空格和换行
	
		if((strcmp(user,username)==0)&&(strcmp(pass,password)==0))
		{
			auth=1;
			break;
		}
	}

	free(line);
	fclose(fd);
	return auth;
}


int ftpServer::ftpserver_start_data_conn()
{	
	int wait;
	if(recv(sock_ctl,&wait,sizeof(wait),0)<0)
	{
		//print_log()
		return -1;
	}

	char buf[1024];
	struct sockaddr_in client_addr;
	socklen_t len=sizeof(client_addr);
	getpeername(sock_ctl,(struct sockaddr*)&client_addr,&len);
	inet_ntop(AF_INET,&client_addr.sin_addr,buf,sizeof(buf));

	int sock_data=socket_connect(buf,CLIENT_PORT);
	if(sock_data<0)
	{
		//print_log()
		return -1;
	}
	return sock_data;
}


int ftpServer::ftpserver_list(int sock_data)
{
	int ret=system("ls -l ftp > temp.txt");	
	if(ret<0)
	{
		//print_log
		return -1;
	}

	int fd=open("temp.txt",O_RDONLY);
	send_response(sock_ctl,1);        //准备发送数据
	struct stat st;
	stat("temp.txt",&st);
	size_t size=st.st_size;

	sendfile(sock_data,fd,NULL,size);
	close(fd);

	send_response(sock_ctl,226);        //发送应答码
	return 0;
}

void ftpServer::ftpserver_retr(int sock_data,char *filename)
{
	string name="ftp/";
	name+=filename;
	int fd=open(name.c_str(),O_RDONLY);
	if(fd<0)
	{
		send_response(sock_ctl,550);        //发送错误码
	}
	else
	{
		send_response(sock_ctl,150);        //发送 ok
		struct stat st;
		stat(name.c_str(),&st);
		size_t size=st.st_size;
		sendfile(sock_data,fd,NULL,size);
		send_response(sock_ctl,226); //文件传输完成
		close(fd);
	}
}


void ftpServer::ftpserver_push(int sock_data,char* filename)
{
	int ack;
	if(recv(sock_ctl,&ack,sizeof(ack),0)<0)
	{
		send_response(sock_ctl,502); //命令执行失败
		return;
	}
	
	int status=ntohl(ack);
	if(553==status)     //客户端上传文件失败
	{
		send_response(sock_ctl,553); 
		return;
	}

	string name="ftp/";
	name+=filename;
	int fd=open(name.c_str(),O_CREAT|O_WRONLY,0664);
	if(fd<0)
	{
		send_response(sock_ctl,502); //命令执行失败
		return;
	}
	
	while(1)
	{
		char data[MAXSIZE];
		memset(data,0,sizeof(data));
		ssize_t s=recv(sock_data,data,sizeof(data),0);
		if(s<=0)
		{
			if(s<0)
				send_response(sock_ctl,502); //命令执行失败
			else
				send_response(sock_ctl,226); //命令执行成功
			break;
		}
		write(fd,data,s);
	}
	close(fd);
}
