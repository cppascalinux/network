#include"socket.h"
#include"ip.h"
#include"device.h"
#include<stdio.h>
#include<string.h>
#include<memory.h>
#include<malloc.h>
#include<pthread.h>
#include<stdlib.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<netdb.h>
#include<semaphore.h>
#include<assert.h>
#include<unistd.h>

static socket_s *socket_head;
static pthread_mutex_t socket_lock=PTHREAD_MUTEX_INITIALIZER;
static int tot_fd=FD_MASK;
static pthread_mutex_t fd_lock=PTHREAD_MUTEX_INITIALIZER;
static uint8_t ports_used[65536];
static pthread_mutex_t ports_lock=PTHREAD_MUTEX_INITIALIZER;
static int totp;
static int inited;

uint32_t max(uint32_t a,uint32_t b)
{
	return a>b?a:b;
}

uint32_t min(uint32_t a,uint32_t b)
{
	return a<b?a:b;
}

void init_socket()
{
	inited=1;
	add_all_devices();
	set_ip_packet_receive_callback(tcp_packet_handler);
	pthread_t router_id,timer_id;
	pthread_create(&router_id,0,router_thread,0);
	pthread_create(&timer_id,0,timer_thread,0);
	// sleep(MAX_HOPS);
}

int send_tcp_packet(const tcp_packet p)
{
	// printf("flags:%d\n",p.flag);
	uint8_t *tcp_buf=calloc(p.len,1);
	*(uint16_t*)(tcp_buf)=p.src.sin_port;
	*(uint16_t*)(tcp_buf+2)=p.dest.sin_port;
	*(uint32_t*)(tcp_buf+4)=htonl(p.seq);
	*(uint32_t*)(tcp_buf+8)=htonl(p.ack);
	tcp_buf[12]=(p.header_len/4)<<4;
	tcp_buf[13]=p.flag;
	*(uint16_t*)(tcp_buf+14)=htons(p.win);
	*(uint16_t*)(tcp_buf+16)=0;// Checksum
	*(uint16_t*)(tcp_buf+18)=0;// Urgent pointer
	for(size_t i=5;i*4<p.header_len;i++)
		*(uint32_t*)(tcp_buf+4*i)=*(uint32_t*)(p.options+4*(i-5));
	if(p.data)
		memcpy(tcp_buf+p.header_len,p.data,p.len-p.header_len);
	int v=send_ip_packet(p.src.sin_addr,p.dest.sin_addr,IPPROTO_TCP,tcp_buf,p.len);
	free(tcp_buf);
	return v;
}

void free_packet(tcp_packet p)
{
	free(p.options);
	free(p.data);
}

void *packet_sender(void *vargp)
{
	void **args=vargp;
	socket_s *s=args[0];
	tcp_packet *p=args[1];
	free(vargp);
	while(1)
	{
		if(send_tcp_packet(*p)<0)
		{
			fprintf(stderr,"Error: packet_sender: failed to send packet to %s:%d\n",
				inet_ntoa(p->dest.sin_addr),htons(p->dest.sin_port));
			// free_packet(*p);
			// free(p);
			// return (void*)((int64_t)-1);
		}
		sleep(RESEND_SEC);
		// printf("qweqweqweqw\n");
		pthread_mutex_lock(&s->lock);
		if(s->send_ack>p->seq)
		{
			free_packet(*p);
			free(p);
			pthread_mutex_unlock(&s->lock);
			// printf("return!\n");
			return 0;
		}
		p->ack=s->recv_ack;
		pthread_mutex_unlock(&s->lock);
		// printf("resend seq: %d ack: %d\n",p->seq,p->ack);
	}
}

void *socket_destructor(void *vargp)
{
	pthread_detach(pthread_self());
	sleep(CLOSE_TIMEOUT);
	socket_s *s=vargp;
	pthread_mutex_lock(&socket_lock);
	if(s==socket_head)
		socket_head=s->next;
	else
	{
		for(socket_s *t=socket_head;t;t=t->next)
			if(t->next==s)
				t->next=s->next;
	}
	pthread_mutex_unlock(&socket_lock);
	pthread_mutex_lock(&ports_lock);
	ports_used[ntohs(s->src.sin_port)]=0;
	pthread_mutex_unlock(&ports_lock);
	free(s->conn);
	free(s);
	return 0;
}

socket_s *get_socket_fd(int fd)
{
	socket_s *ret=0;
	pthread_mutex_lock(&socket_lock);
	for(socket_s *p=socket_head;p;p=p->next)
		if(p->fd==fd)
			ret=p;
	pthread_mutex_unlock(&socket_lock);
	return ret;
}

int equal_sockaddr(struct sockaddr_in a,struct sockaddr_in b)
{
	return a.sin_addr.s_addr==b.sin_addr.s_addr&&
		a.sin_port==b.sin_port;
}

socket_s *get_socket_addr(struct sockaddr_in src,struct sockaddr_in dest,uint16_t states)
{
	socket_s *ret=0;
	pthread_mutex_lock(&socket_lock);
	for(socket_s *p=socket_head;p;p=p->next)
	{
		if(equal_sockaddr(src,p->src)&&equal_sockaddr(dest,p->dest)&&(p->state&states))
		{
			ret=p;
			break;
		}
		// Use a wildcard ip address
		if(p->src.sin_addr.s_addr==0&&p->dest.sin_addr.s_addr==0
			&&p->src.sin_port==src.sin_port&&p->dest.sin_port==0&&(p->state&states))
				ret=p;
	}
	pthread_mutex_unlock(&socket_lock);
	return ret;
}

tcp_packet get_tcp_packet(uint8_t *buf,size_t len)
{
	tcp_packet p;
	memset(&p,0,sizeof(p));
	uint8_t ip_header_len=(buf[0]&0xF)*4;
	uint8_t *tcp_buf=buf+ip_header_len;
	p.src.sin_addr.s_addr=*(uint32_t*)(buf+12);
	p.dest.sin_addr.s_addr=*(uint32_t*)(buf+16);
	p.src.sin_port=*(uint16_t*)tcp_buf;
	p.dest.sin_port=*(uint16_t*)(tcp_buf+2);
	p.seq=ntohl(*(uint32_t*)(tcp_buf+4));
	p.ack=ntohl(*(uint32_t*)(tcp_buf+8));
	uint8_t tcp_header_len=(tcp_buf[12]>>4)*4;
	p.len=len-ip_header_len;
	p.header_len=tcp_header_len;
	p.flag=tcp_buf[13];
	p.win=ntohs(*(uint16_t*)(tcp_buf+14));
	if(tcp_header_len>20)
	{
		p.options=calloc(tcp_header_len-20,1);
		memcpy(p.options,tcp_buf+20,tcp_header_len-20);
	}
	if(p.header_len<p.len)
	{
		p.data=calloc(p.len-p.header_len,1);
		memcpy(p.data,tcp_buf+tcp_header_len,p.len-p.header_len);
	}
	return p;
}

int syn_handler(tcp_packet p)
{
	// printf("SYNQWQ\n");
	socket_s *s=get_socket_addr(p.dest,p.src,SYN_RCVD|ESTABLISHED);
	if(s)// we have already handled this SYN packet, simply ignore this one
		return 0;
	s=get_socket_addr(p.dest,p.src,LISTEN);
	if(!s)
	{
		fprintf(stderr,"Error: syn_handler: could not find listen socket at %s:%d\n",
			inet_ntoa(p.dest.sin_addr),ntohs(p.dest.sin_port));
		return -1;
	}
	if(s->num_conn==s->max_conn)// the connection buffer is full
		return -1;
	int fd=__wrap_socket(AF_INET,SOCK_STREAM,0);// create a new socket for read/write
	socket_s *ns=get_socket_fd(fd);
	pthread_mutex_lock(&ns->lock);
	ns->src=p.dest;
	ns->dest=p.src;
	ns->state=SYN_RCVD;
	ns->recv_ack=ns->recv_seq=p.seq+1;
	ns->send_seq=ns->send_ack=rand();
	if(p.header_len>20)
	{
		assert(p.options);
		for(size_t i=0;i*4+20<p.header_len;i++)
		{
			if(p.options[4*i]==2&&p.options[4*i+1]==4)// this is MSS option
			{
				ns->mtu=ntohs(*(uint16_t*)(p.options+4*i+2));
			}
		}
	}
	tcp_packet *np=calloc(1,sizeof(tcp_packet));// construct a SYN/ACK packet
	np->src=ns->src;
	np->dest=ns->dest;
	np->seq=ns->send_seq++;
	np->ack=ns->recv_ack;
	np->len=np->header_len=24;
	np->flag=SYN|ACK;
	np->win=SOCKET_BUF_SIZE-(ns->recv_ack-ns->recv_seq);
	np->options=calloc(4,1);
	np->options[0]=2;
	np->options[1]=4;
	pthread_mutex_unlock(&ns->lock);
	*(uint16_t*)(np->options+2)=htons(MTU);
	void **args=calloc(2,sizeof(void*));
	args[0]=ns;
	args[1]=np;
	pthread_t tid;
	pthread_create(&tid,0,packet_sender,args);
	pthread_detach(tid);

	pthread_mutex_lock(&s->lock);// add ns to the connection buffer
	if(!s->num_conn)
		sem_post(&s->conn_sem);
	s->conn[s->num_conn++]=ns;
	pthread_mutex_unlock(&s->lock);

	return 0;
}

int syn_ack_handler(tcp_packet p)
{
	// printf("SYN/ACKQWQ\n");
	socket_s *s=get_socket_addr(p.dest,p.src,SYN_SENT|ESTABLISHED);
	if(!s)// socket not found
	{
		fprintf(stderr,"Error: syn_ack_handler: couldn't find socket at %s:%d\n",
			inet_ntoa(p.dest.sin_addr),ntohs(p.dest.sin_port));
		return -1;
	}
	pthread_mutex_lock(&s->lock);
	if(s->state==SYN_SENT)
	{
		s->state=ESTABLISHED;
		if(p.header_len>20)
		{
			assert(p.options);
			for(size_t i=0;i*4+20<p.header_len;i++)
			{
				if(p.options[4*i]==2&&p.options[4*i+1]==4)// this is MSS option
				{
					s->mtu=ntohs(*(uint16_t*)(p.options+4*i+2));
				}
			}
		}
	}
	s->send_ack=max(s->send_ack,p.ack);
	s->recv_ack=max(s->recv_ack,p.seq+1);
	s->recv_seq=max(s->recv_seq,p.seq+1);
	tcp_packet np;
	memset(&np,0,sizeof(tcp_packet));
	np.src=s->src;
	np.dest=s->dest;
	np.seq=s->send_seq;
	np.ack=s->recv_ack;
	np.flag=ACK;
	np.win=SOCKET_BUF_SIZE-(s->recv_ack-s->recv_seq);
	np.len=np.header_len=20;
	pthread_mutex_unlock(&s->lock);
	// printf("SYN/ACKRETURNQWQ\n");
	return send_tcp_packet(np);
}

int ack_handler(tcp_packet p)
{
	// printf("ACKQWQ");
	// printf("ack seq:%d ack:%d\n",p.seq,p.ack);
	socket_s *s=get_socket_addr(p.dest,p.src,ALL_STATES^LISTEN^SYN_SENT);
	if(!s)
	{
		fprintf(stderr,"Error: ack_handler: couldn't find socket at %s:%d\n",
			inet_ntoa(p.dest.sin_addr),ntohs(p.dest.sin_port));
		return -1;
	}
	pthread_mutex_lock(&s->lock);
	if(s->state==SYN_RCVD&&p.ack==s->send_seq)
	{
		s->state=ESTABLISHED;
		sem_post(&s->conn_sem);
	}
	if(p.ack>s->send_ack)
	{
		int df=(s->send_seq==s->send_ack+SOCKET_BUF_SIZE);
		s->send_ack=p.ack;
		if(df)
			sem_post(&s->write_sem);
	}
	int pr=(s->state!=TIMED_WAIT);
	if(s->send_ack==s->send_seq)
	{
		if(s->state&FIN_WAIT_1)
			s->state=FIN_WAIT_2;
		if(s->state&CLOSING)
			s->state=TIMED_WAIT;
		if(s->state&LAST_ACK)
			s->state=TIMED_WAIT;
	}
	if(pr&&s->state==TIMED_WAIT)// The first time we entered TIMED_WAIT
	{
		pthread_t tid;
		pthread_create(&tid,0,socket_destructor,s);// Call the timed destructor
	}
	size_t data_len=p.len-p.header_len;
	if(data_len)// send an ACK packet
	{
		// printf("data_len: %ld recv_ack: %d recv_seq: %d\n",data_len,s->recv_ack,s->recv_seq);
		if(p.seq>=s->recv_ack&&p.seq+data_len<=s->recv_seq+SOCKET_BUF_SIZE)
		{// the message can be stored in the buffer
			for(size_t i=0;i<data_len;i++)
			{
				s->recv_buf[(i+p.seq)%SOCKET_BUF_SIZE]=p.data[i];
				s->recv_valid[(i+p.seq)%SOCKET_BUF_SIZE]=1;
			}
			int df=(s->recv_ack==s->recv_seq);
			while(s->recv_ack<s->recv_seq+SOCKET_BUF_SIZE&&s->recv_valid[s->recv_ack%SOCKET_BUF_SIZE])
				s->recv_ack++;
			if(df&&s->recv_ack>s->recv_seq)// we can now read the buffer
				sem_post(&s->read_sem);
		}
		tcp_packet np;
		memset(&np,0,sizeof(np));
		np.src=s->src;
		np.dest=s->dest;
		np.seq=s->send_seq;
		np.ack=s->recv_ack;
		np.flag=ACK;
		np.win=SOCKET_BUF_SIZE-(s->recv_ack-s->recv_seq);
		np.len=np.header_len=20;
		pthread_mutex_unlock(&s->lock);
		return send_tcp_packet(np);
	}
	pthread_mutex_unlock(&s->lock);
	return 0;
}

int fin_handler(tcp_packet p)
{
	// printf("FINQWQ\n");
	socket_s *s=get_socket_addr(p.dest,p.src,ALL_STATES^LISTEN^SYN_SENT);
	if(!s)
	{
		fprintf(stderr,"Error: fin_handler: Couldn't find socket to finish at %s:%d\n",
			inet_ntoa(p.dest.sin_addr),p.dest.sin_port);
		return -1;
	}
	pthread_mutex_lock(&s->lock);
	int pr=(s->state!=TIMED_WAIT);
	if(s->recv_ack==p.seq)// We can ACK this FIN packet
	{
		s->recv_ack++;
		if(s->recv_ack==s->recv_seq+1)
			sem_post(&s->read_sem);
		if(s->state&(SYN_RCVD|ESTABLISHED))
			s->state=CLOSE_WAIT;
		if(s->state&FIN_WAIT_1)
			s->state=CLOSING;
		if(s->state&FIN_WAIT_2)
			s->state=TIMED_WAIT;
	}
	int wr=(s->send_seq>=s->send_ack+SOCKET_BUF_SIZE);
	s->send_ack=max(s->send_ack,p.ack);
	if(wr&&s->send_seq<s->send_ack+SOCKET_BUF_SIZE)
		sem_post(&s->write_sem);
	if(s->send_ack==s->send_seq)// a good ACK number
	{
		if(s->state&FIN_WAIT_1)
			s->state=FIN_WAIT_2;
		if(s->state&CLOSING)
			s->state=TIMED_WAIT;
		if(s->state&LAST_ACK)
			s->state=TIMED_WAIT;
	}
	if(pr&&s->state==TIMED_WAIT)// The first time we entered TIMED_WAIT
	{
		pthread_t tid;
		pthread_create(&tid,0,socket_destructor,s);
	}
	tcp_packet np;// Construct an ACK packet
	memset(&np,0,sizeof(np));
	np.src=s->src;
	np.dest=s->dest;
	np.seq=s->send_seq;
	np.ack=s->recv_ack;
	np.flag=ACK;
	np.win=SOCKET_BUF_SIZE-(s->recv_ack-s->recv_seq);
	np.len=np.header_len=20;
	pthread_mutex_unlock(&s->lock);
	return send_tcp_packet(np);
}

int tcp_packet_handler(const void *buf,int len)
{
	if(tcp_checksum((void*)buf,len,0)!=0xFFFF)
	{
		fprintf(stderr,"Error: tcp_packet_handler: checksum validation failed\n");
		return -1;
	}
	pthread_mutex_lock(&fd_lock);
	// printf("packet #%d received\n",++totp);
	pthread_mutex_unlock(&fd_lock);
	tcp_packet p=get_tcp_packet((uint8_t*)buf,len);
	if(p.flag&SYN)// this is a SYN packet
	{
		int v;
		if(!(p.flag&ACK))// this is the first SYN packet
			v=syn_handler(p);
		else// this is a SYN/ACK packet
			v=syn_ack_handler(p);
		free_packet(p);
		return v;
	}
	else if(p.flag&FIN)// this is a FIN packet
	{
		int v=fin_handler(p);
		free_packet(p);
		return v;
	}
	else if(p.flag&ACK)// this is an ACK packet
	{
		int v=ack_handler(p);
		free_packet(p);
		return v;
	}
	else
	{
		fprintf(stderr,"Error: tcp_packet_handler: no flag for tcp packet");
		return -1;
	}
}

int __wrap_socket(int domain,int type,int protocol)
{
	if(!inited)
		init_socket();
	if(domain!=AF_INET)
	{
		fprintf(stderr,"Error: socket: support AF_INET for domain only\n");
		return -1;
	}
	if(type!=SOCK_STREAM)
	{
		fprintf(stderr,"Error: socket: support SOCK_STREAM for type only\n");
		return -1;
	}
	if(protocol&&protocol!=IPPROTO_TCP)
	{
		fprintf(stderr,"Error: socket: support IPPROTO_TCP for protocol only\n");
		return -1;
	}
	socket_s *s=calloc(1,sizeof(socket_s));
	pthread_mutex_lock(&fd_lock);
	s->fd=tot_fd++;
	pthread_mutex_unlock(&fd_lock);
	sem_init(&s->read_sem,0,0);
	sem_init(&s->write_sem,0,1);
	sem_init(&s->conn_sem,0,0);
	pthread_mutex_init(&s->lock,0);
	s->state=CLOSED;
	s->mtu=MTU;

	pthread_mutex_lock(&socket_lock);
	s->next=socket_head;
	socket_head=s;
	pthread_mutex_unlock(&socket_lock);
	return s->fd;
}

int __wrap_setsockopt(int sockfd,int level,int optname,const void *optval,socklen_t optlen)
{
	return 0;// haha
}

int __wrap_bind(int sockfd,const struct sockaddr *addr,socklen_t addrlen)
{
	socket_s *s=get_socket_fd(sockfd);
	if(!s)
	{
		fprintf(stderr,"Error: __wrap_bind: could not find socket fd %d\n",sockfd);
		return -1;
	}
	pthread_mutex_lock(&s->lock);
	s->src=*(struct sockaddr_in*)addr;
	uint16_t port=ntohs(s->src.sin_port);
	pthread_mutex_unlock(&s->lock);
	pthread_mutex_lock(&ports_lock);
	ports_used[port]=1;
	pthread_mutex_unlock(&ports_lock);
	return 0;
}

int __wrap_listen(int sockfd,int backlog)
{
	socket_s *s=get_socket_fd(sockfd);
	if(!s)
	{
		fprintf(stderr,"Error: __wrap_listen: could not find socket fd %d\n",sockfd);
		return -1;
	}
	pthread_mutex_lock(&s->lock);
	s->state=LISTEN;
	s->max_conn=backlog;
	s->num_conn=0;
	s->conn=calloc(backlog,sizeof(socket_s*));
	pthread_mutex_unlock(&s->lock);
	return 0;
}

int __wrap_connect(int sockfd,const struct sockaddr *addr,socklen_t addrlen)
{
	socket_s *s=get_socket_fd(sockfd);
	if(!s)
	{
		fprintf(stderr,"Error: __wrap_connect: could not find socket fd %d\n",sockfd);
		return -1;
	}
	if(!dev_head)
	{
		fprintf(stderr,"Error: __wrap_connect: no device added yet\n");
		return -1;
	}
	uint16_t port=0;
	pthread_mutex_lock(&ports_lock);
	for(size_t i=MIN_PORT;i<TOTAL_PORTS;i++)
		if(!ports_used[i])
		{
			ports_used[i]=1;
			port=i;
			break;
		}
	pthread_mutex_unlock(&ports_lock);
	device_t *d=dev_head;
	pthread_mutex_lock(&s->lock);
	s->dest=*(struct sockaddr_in*)addr;
	s->src.sin_addr=d->ip_addr;
	s->src.sin_port=htons(port);
	s->src.sin_family=AF_INET;
	s->send_seq=rand();
	s->state=SYN_SENT;
	
	tcp_packet *p=calloc(1,sizeof(tcp_packet));
	p->src=s->src;
	p->dest=s->dest;
	p->seq=s->send_seq++;
	p->header_len=p->len=24;
	p->flag=SYN;
	p->win=SOCKET_BUF_SIZE-(s->recv_ack-s->recv_seq);
	p->options=calloc(4,1);
	p->options[0]=0x2;
	p->options[1]=0x4;
	*(uint16_t*)(p->options+2)=htons(MTU);
	pthread_mutex_unlock(&s->lock);

	void **args=calloc(2,sizeof(void*));
	args[0]=s;
	args[1]=p;
	pthread_t tid;
	void *ret;
	pthread_create(&tid,0,packet_sender,args);
	pthread_join(tid,&ret);
	// printf("connectreturn\n");
	return (int)((int64_t)ret);
}

int __wrap_accept(int sockfd,struct sockaddr *addr,socklen_t *addrlen)
{
	socket_s *s=get_socket_fd(sockfd);
	if(!s)
	{
		fprintf(stderr,"Error: __wrap_accept: couldn't find socket fd %d\n",sockfd);
		return -1;
	}
	sem_wait(&s->conn_sem);
	pthread_mutex_lock(&s->lock);
	socket_s *ns=s->conn[--s->num_conn];
	if(s->num_conn)
		sem_post(&s->conn_sem);
	pthread_mutex_unlock(&s->lock);
	if(addr)
		*addr=*(struct sockaddr*)(&ns->dest);
	if(addrlen)
		*addrlen=sizeof(struct sockaddr_in);
	sem_wait(&ns->conn_sem);
	return ns->fd;
}

ssize_t __wrap_read(int fd,void *buf,size_t count)
{
	if(!(fd&FD_MASK))
		return __real_read(fd,buf,count);
	socket_s *s=get_socket_fd(fd);
	if(!s)// Could not find socket
	{
		fprintf(stderr,"Error: __wrap_read: Couldn't find socket fd %d\n",fd);
		return -1;
	}
	pthread_mutex_lock(&s->lock);
	if(s->state&(FIN_WAIT_1|FIN_WAIT_2|CLOSING|TIMED_WAIT|LAST_ACK))
	{
		fprintf(stderr,"Error: __wrap_read: socket fd %d already closed\n",fd);
		pthread_mutex_unlock(&s->lock);
		return -1;
	}
	// printf("recv_ack:%d recv_seq:%d state:%d\n",s->recv_ack,s->recv_seq,s->state);
	pthread_mutex_unlock(&s->lock);
	sem_wait(&s->read_sem);
	pthread_mutex_lock(&s->lock);
	ssize_t num=min(count,s->recv_ack-s->recv_seq);
	if(s->state&CLOSE_WAIT)
		num--;
	if(!num)
	{
		// printf("read peer closed qwq\n");
		sem_post(&s->read_sem);
		pthread_mutex_unlock(&s->lock);
		return 0;
	}
	uint8_t *cbuf=buf;
	for(size_t i=0;i<num;i++)
	{
		cbuf[i]=s->recv_buf[s->recv_seq%SOCKET_BUF_SIZE];
		s->recv_valid[s->recv_seq%SOCKET_BUF_SIZE]=0;
		s->recv_seq++;
	}
	if(s->recv_seq<s->recv_ack)
		sem_post(&s->read_sem);
	pthread_mutex_unlock(&s->lock);
	return num;
}

ssize_t __wrap_write(int fd,void *buf,size_t count)
{
	if(!(fd&FD_MASK))
		return __real_write(fd,buf,count);
	socket_s *s=get_socket_fd(fd);
	if(!s)// Could not find socket
	{
		fprintf(stderr,"Error: __wrap_write: Couldn't find socket fd %d\n",fd);
		return -1;
	}
	pthread_mutex_lock(&s->lock);
	if(s->state&(FIN_WAIT_1|FIN_WAIT_2|CLOSING|TIMED_WAIT|LAST_ACK))
	{
		fprintf(stderr,"Error: __wrap_write: socket fd %d already closed\n",fd);
		pthread_mutex_unlock(&s->lock);
		return -1;
	}
	if(s->state&CLOSE_WAIT)// peer closed, could not write
	{
		pthread_mutex_unlock(&s->lock);
		// printf("write peer closed qwq\n");
		return 0;
	}
	pthread_mutex_unlock(&s->lock);
	sem_wait(&s->write_sem);
	pthread_mutex_lock(&s->lock);
	size_t num=min(count,SOCKET_BUF_SIZE-(s->send_seq-s->send_ack));
	for(size_t i=0;i<num;i+=s->mtu)
	{
		size_t len=min(s->mtu,num-i);
		tcp_packet *p=calloc(1,sizeof(tcp_packet));
		p->src=s->src;
		p->dest=s->dest;
		p->seq=s->send_seq;
		s->send_seq+=len;
		p->ack=s->recv_ack;
		p->header_len=20;
		p->len=p->header_len+len;
		p->flag=ACK;
		p->win=SOCKET_BUF_SIZE-(s->recv_ack-s->recv_seq);
		p->data=calloc(len,1);
		memcpy(p->data,buf+i,len);
		void **args=calloc(2,sizeof(void*));
		args[0]=s;
		args[1]=p;
		pthread_t tid;
		pthread_create(&tid,0,packet_sender,args);
		pthread_detach(tid);
	}
	if(s->send_seq<s->send_ack+SOCKET_BUF_SIZE)
		sem_post(&s->write_sem);
	pthread_mutex_unlock(&s->lock);
	return num;
}

// int remove_socket(socket_s *s)
// {
// 	if(!s)
// 		return -1;
// 	pthread_mutex_lock(&s->lock);
// 	free(s->conn);// Free the allocated connection buffer for s
// 	uint16_t port=ntohs(s->src.sin_port);
// 	socket_s *ns=s->next;
// 	pthread_mutex_unlock(&s->lock);

// 	pthread_mutex_lock(&ports_lock);
// 	ports[ports_top++]=port;// Put the port number back
// 	pthread_mutex_unlock(&ports_lock);

// 	pthread_mutex_lock(&socket_lock);
// 	if(s==socket_head)
// 	{
// 		socket_head=ns;
// 		pthread_mutex_unlock(&socket_lock);
// 	}
// 	else
// 	{
// 		socket_s *r;
// 		for(r=socket_head;r;r=r->next)
// 			if(r->next==s)
// 				break;
// 		pthread_mutex_unlock(&socket_lock);
// 		pthread_mutex_lock(&r->lock);
// 		r->next=ns;
// 		pthread_mutex_unlock(&r->lock);
// 	}
// 	free(s);
// 	return 0;
// }

int close_normal(socket_s *s)
{
	pthread_mutex_lock(&s->lock);
	if(!(s->state&(SYN_RCVD|ESTABLISHED|CLOSE_WAIT)))
	{
		fprintf(stderr,"Error: close_normal: socket state is %d\n",s->state);
		pthread_mutex_unlock(&s->lock);
		return -1;
	}
	if(s->state&(SYN_RCVD|ESTABLISHED))
		s->state=FIN_WAIT_1;
	else if(s->state&CLOSE_WAIT)
		s->state=LAST_ACK;
	tcp_packet *p=calloc(1,sizeof(tcp_packet));
	p->src=s->src;
	p->dest=s->dest;
	p->seq=s->send_seq++;
	p->ack=s->recv_ack;
	p->flag=FIN|ACK;
	p->len=p->header_len=20;
	p->win=SOCKET_BUF_SIZE-(s->recv_ack-s->recv_seq);
	pthread_mutex_unlock(&s->lock);
	void **args=calloc(2,sizeof(void*));
	args[0]=s;
	args[1]=p;
	pthread_t tid;
	void *ret;
	pthread_create(&tid,0,packet_sender,args);
	pthread_join(tid,&ret);
	// pthread_detach(tid);
	return 0;
}

int __wrap_close(int fd)
{
	if(!(fd&FD_MASK))
		return __real_close(fd);
	socket_s *s=get_socket_fd(fd);
	if(!s)
	{
		fprintf(stderr,"Error: __wrap_close: Couldn't find socket fd %d\n",fd);
		return -1;
	}
	pthread_mutex_lock(&s->lock);
	if(s->state==LISTEN)// This is a listen socket
	{
		s->state=TIMED_WAIT;
		for(size_t i=0;i<s->num_conn;i++)
			close_normal(s->conn[i]);
		pthread_mutex_unlock(&s->lock);
		pthread_t tid;
		pthread_create(&tid,0,socket_destructor,s);
		return 0;
	}
	else
	{
		pthread_mutex_unlock(&s->lock);
		return close_normal(s);
	}
	return 0;
}

int __wrap_getaddrinfo(const char *node,const char *service,const struct addrinfo *hints,struct addrinfo **res)
{
	struct addrinfo *p=calloc(1,sizeof(struct addrinfo));
	p->ai_family=AF_INET;
	p->ai_socktype=SOCK_STREAM;
	p->ai_protocol=IPPROTO_TCP;
	p->ai_addrlen=sizeof(struct sockaddr_in);
	struct sockaddr_in *s=calloc(1,sizeof(struct sockaddr_in));
	s->sin_family=AF_INET;
	if(node)
		inet_aton(node,&s->sin_addr);
	if(service)
	{
		int v;
		sscanf(service,"%d",&v);
		s->sin_port=htons((uint16_t)v);
	}
	p->ai_addr=(struct sockaddr*)s;
	char *nm=calloc(7,1);
	strcpy(nm,"(null)");
	p->ai_canonname=nm;
	*res=p;
	return 0;
}

int __wrap_freeaddrinfo(struct addrinfo *res)
{
	for(struct addrinfo *p=res,*q=res->ai_next;p;p=q,q=p->ai_next)
	{
		free(p->ai_addr);
		free(p->ai_canonname);
		free(p);
	}
	return 0;
}