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
int main(int argc,char *argv[])
{
	if(argc!=2)
	{
		printf("Usage: %s <filename>\n",argv[0]);
		return -1;
	}
	int fd;
	if((fd=open(argv[1],O_RDWR))<0)
	{
		fprintf(stderr,"Error: Couldn't open file %s",argv[1]);
		return -1;
	}
	struct stat sb;
	if(fstat(fd,&sb)<0)
	{
		fprintf(stderr,"Error: Couldn't get file state for %s",argv[1]);
		return -1;
	}
	off_t len=sb.st_size;
	printf("len:%ld\n",len);
	uint8_t *buf=mmap(0,len,PROT_READ,MAP_PRIVATE,fd,0);
	// for(int i=0;i<len;i++)
	// 	printf("%c",buf[i]);
	int listenfd=socket(AF_INET,SOCK_STREAM,0);
	struct sockaddr_in servaddr;
	memset(&servaddr,0,sizeof(struct sockaddr_in));
	servaddr.sin_addr.s_addr=0;
	servaddr.sin_family=AF_INET;
	servaddr.sin_port=htons(SERVER_PORT);
	bind(listenfd,(struct sockaddr*)&servaddr,sizeof(struct sockaddr));
	listen(listenfd,256);
	int connfd=accept(listenfd,0,0);
	if(connfd<0)
	{
		printf("Error: Couldn't connect to client\n");
		return -1;
	}
	uint8_t *cbuf=buf;
	int rem=len;
	while(rem)
	{
		int c=write(connfd,cbuf,rem);
		if(c==0)
		{
			printf("Error: peer closed\n");
			break;
		}
		else if(c<0)
		{
			if(errno==EINTR)
				continue;
			else
			{
				printf("Error #%d: %s\n",errno,strerror(errno));
				break;
			}
		}
		rem-=c;
		cbuf+=c;
		// printf("rem:%d\n",rem);
	}
	close(connfd);
	close(fd);
	munmap(buf,len);
	sleep(10);
	return 0;
}