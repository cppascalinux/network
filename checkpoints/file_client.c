#include<sys/stat.h>
#include<sys/mman.h>
#include<stdio.h>
#include<fcntl.h>
#include<stdint.h>
#include<unistd.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<string.h>
#include<errno.h>
#define SERVER_PORT 39863
#define BUF_SIZE 1048576
uint8_t buf[BUF_SIZE];
int main(int argc,char *argv[])
{
	if(argc!=2)
	{
		printf("Usage: %s <serverIP>\n",argv[0]);
		return -1;
	}
	FILE *f=fopen("recv_file","w");
	int connfd=socket(AF_INET,SOCK_STREAM,0);
	struct sockaddr_in addr;
	memset(&addr,0,sizeof(addr));
	addr.sin_family=AF_INET;
	addr.sin_addr.s_addr=inet_addr(argv[1]);
	// printf("addr:%d\n",addr.sin_addr.s_addr);
	addr.sin_port=htons(SERVER_PORT);
	if(connect(connfd,(struct sockaddr*)&addr,sizeof(addr))<0)
	{
		printf("Error: Couldn't connect to address %s:%d",argv[1],SERVER_PORT);
		return -1;
	}
	// printf("connected qwq\n");
	int sum=0;
	while(1)
	{
		int c=read(connfd,buf,BUF_SIZE);
		if(c==0)
			break;
		if(c<0)
		{
			if(errno==EINTR)
				continue;
			else
			{
				printf("%s\n",strerror(errno));
				break;
			}
		}
		fwrite(buf,1,c,f);
		sum+=c;
	}
	printf("total length: %d\n",sum);
	fclose(f);
	close(connfd);
	sleep(10);
	return 0;
}