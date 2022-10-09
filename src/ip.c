#include"ip.h"
#include"device.h"
#include"packetio.h"
#include<stdio.h>
#include<pthread.h>
#include<malloc.h>
#include<string.h>
#include<memory.h>
#include<unistd.h>
#include<netinet/ip.h>
#include<netinet/in.h>
#include<netinet/ether.h>
#include<arpa/inet.h>

pthread_mutex_t route_lock=PTHREAD_MUTEX_INITIALIZER;
route_t *route_head;
static ip_packet_receive_callback global_callback;
static pthread_mutex_t callback_lock=PTHREAD_MUTEX_INITIALIZER;

unsigned int reverse_bytes(const struct in_addr addr)
{
	unsigned int x=0;
	unsigned int y=addr.s_addr;
	x|=(y>>24)&0xFF;
	x|=(y>>8)&0xFF00;
	x|=(y<<8)&0xFF0000;
	x|=(y<<24)&0xFF000000;
	return x;
}

int select_routing_entry(const struct in_addr dest,route_t *r)
{
	pthread_mutex_lock(&route_lock);
	unsigned int max_mask=0;
	int suc=0;
	for(route_t *p=route_head;p;p=p->next)
		if((dest.s_addr&p->mask.s_addr)==p->dest.s_addr)
		{
			if(!suc||p->mask.s_addr>max_mask)
			{
				max_mask=p->mask.s_addr;
				*r=*p;
			}
			suc=1;
		}
	pthread_mutex_unlock(&route_lock);
	if(!suc)
		return -1;
	return 0;
}

int send_ip_packet(const struct in_addr src,const struct in_addr dest,int proto,const void *buf,int len)
{
	route_t rt;
	if(select_routing_entry(dest,&rt)<0)
	{
		char s[INET_ADDRSTRLEN];
		inet_ntop(AF_INET,&dest,s,INET_ADDRSTRLEN);
		fprintf(stderr,"Error: send_ip_packet: could not find address %s in routing table\n",s);
		return -1;
	}
	unsigned char *s=calloc(len+20,1);
	memcpy(s+20,buf,len);
	len+=20;
	s[0]=0x45;
	s[1]=0;
	s[2]=len>>8&0xFF;
	s[3]=len&0xFF;
	s[4]=s[5]=0;
	s[6]=0x40;// Don't fragment!
	s[7]=0;
	s[8]=0xFF;// TTL:255
	s[9]=proto;
	s[10]=s[11]=0;// Ignore header checksum
	*(unsigned int*)(s+12)=src.s_addr;
	*(unsigned int*)(s+16)=dest.s_addr;
	int v=send_frame(s,len,ETH_TYPE_IP,rt.mac,rt.id);
	if(v<0)
		fprintf(stderr,"Error: send_ip_packet: could not send package on device %s\n",find_device_id(rt.id)->name);
	free(s);
	return v;
}

int forward_ip_packet(const void *buf,int len)
{
	const unsigned char *temp=buf;
	temp+=8;
	if(*temp==0)// ttl expired
		return -1;
	unsigned char *nbuf=calloc(len,1);
	memcpy(nbuf,buf,len);
	nbuf[8]--;// tick ttl
	struct in_addr dest;
	dest.s_addr=*(unsigned int*)(nbuf+16);
	route_t rt;
	if(select_routing_entry(dest,&rt)<0)
	{
		char s[INET_ADDRSTRLEN];
		inet_ntop(AF_INET,&dest,s,INET_ADDRSTRLEN);
		fprintf(stderr,"Error: forward_ip_packet: could not find address %s in routing table\n",s);
		free(nbuf);
		return -1;
	}
	pthread_mutex_unlock(&route_lock);
	int v=send_frame(nbuf,len,ETH_TYPE_IP,rt.mac,rt.id);
	if(v<0)
		fprintf(stderr,"Error: forward_ip_packet: could not forward package on device %s\n",find_device_id(rt.id)->name);
	free(nbuf);
	return v;
}

int set_ip_packet_receive_callback(ip_packet_receive_callback callback)
{
	pthread_mutex_lock(&callback_lock);
	global_callback=callback;
	pthread_mutex_unlock(&callback_lock);
	return 0;
}

int update_routing_entry(const struct in_addr dest,const struct in_addr mask,
	int id,struct ether_addr mac,int hops,int timeout)
{
	route_t *r=calloc(1,sizeof(route_t));
	r->dest=dest;
	r->mask=mask;
	r->dest.s_addr&=mask.s_addr;
	r->id=id;
	r->mac=mac;
	r->hops=hops;
	r->timeout=timeout;
	pthread_mutex_lock(&route_lock);
	for(route_t *p=route_head;p;p=p->next)
		if(p->dest.s_addr==r->dest.s_addr&&p->mask.s_addr==r->mask.s_addr)
		{
			if(r->hops<p->hops||(r->hops==p->hops&&(r->timeout==-1||
				(p->timeout>=0&&r->timeout>p->timeout))))
			{
				r->next=p->next;
				*p=*r;
				free(r);
				pthread_mutex_unlock(&route_lock);
				return 0;
			}
			else
			{
				free(r);
				pthread_mutex_unlock(&route_lock);
				return -1;
			}
		}
	r->next=route_head;
	route_head=r;
	pthread_mutex_unlock(&route_lock);
	return 0;
}

int route_local_device()
{
	pthread_mutex_lock(&dev_lock);
	struct in_addr mask;
	mask.s_addr=0xFFFFFFFF;
	struct ether_addr mac;
	memset(mac.ether_addr_octet,0,6);
	int i=0;
	for(device_t *p=dev_head;p;p=p->next)
		i++;
	int tot=i;
	struct in_addr *buf=calloc(i,sizeof(struct in_addr));
	i=0;
	for(device_t *p=dev_head;p;p=p->next)
		buf[i++]=p->ip_addr;
	pthread_mutex_unlock(&dev_lock);
	for(i=0;i<tot;i++)
		update_routing_entry(buf[i],mask,0,mac,0,ROUTE_TIMEOUT);
	free(buf);
	return 0;
}

int tick_routing_table()
{
	pthread_mutex_lock(&route_lock);
	route_t **p=&route_head,*q=route_head;
	while(q)
	{
		if(q->timeout>0)
			q->timeout--;
		if(q->timeout==0)
		{
			*p=q->next;
			free(q);
			q=*p;
		}
		else
		{
			p=&q->next;
			q=*p;
		}
	}
	pthread_mutex_unlock(&route_lock);
	return 0;
}

int broadcast_distance_vector()
{
	pthread_mutex_lock(&route_lock);
	int i=0;
	route_t *p;
	for(p=route_head;p;p=p->next)
		i++;
	int len=3*4*i+4;
	unsigned char *buf=calloc(len,1);
	*(unsigned int*)buf=i;
	i=4;
	for(p=route_head;p;p=p->next)
	{
		*(unsigned int*)(buf+i)=p->dest.s_addr;
		i+=4;
		*(unsigned int*)(buf+i)=p->mask.s_addr;
		i+=4;
		*(unsigned int*)(buf+i)=p->hops;
		i+=4;
	}
	pthread_mutex_unlock(&route_lock);
	struct ether_addr broadcast;
	ether_aton_r("ff:ff:ff:ff:ff:ff",&broadcast);
	if(broadcast_frame(buf,len,ETH_TYPE_ROUTE,broadcast)<0)
	{
		fprintf(stderr,"Error: broad_cast_distance_vector: couldn't broadcast distance vector\n");
		return -1;
	}
	return 0;
}

int update_distance_vector(int id,const unsigned char *buf,int len)
{
	struct ether_addr src;
	for(int i=0;i<6;i++)
		src.ether_addr_octet[i]=buf[i+6];
	int tot=*(unsigned int*)(buf+14);
	const unsigned int *p=(const unsigned int*)(buf+18);
	for(int i=1;i<=tot;i++)
	{
		struct in_addr dest,mask;
		dest.s_addr=p[i*3-3];
		mask.s_addr=p[i*3-2];
		int hops=p[i*3-1];
		if(hops+1<=MAX_HOPS)
			update_routing_entry(dest,mask,id,src,hops+1,ROUTE_TIMEOUT);
	}
	return 0;
}

int check_mac(struct ether_addr mac)
{
	pthread_mutex_lock(&dev_lock);
	int fail=0;
	for(device_t *p=dev_head;p;p=p->next)
	{
		int dif=0;
		for(int i=0;i<6;i++)
			if(p->addr.ether_addr_octet[i]!=mac.ether_addr_octet[i])
				dif=1;
		if(!dif)
			fail=-1;
	}
	pthread_mutex_unlock(&dev_lock);
	return fail;
}

int check_ip_addr(struct in_addr ip)
{
	pthread_mutex_lock(&dev_lock);
	int fail=0;
	for(device_t *p=dev_head;p;p=p->next)
	{
		if(p->ip_addr.s_addr==ip.s_addr)
			fail=-1;
	}
	pthread_mutex_unlock(&dev_lock);
	return fail;
}

int packet_preprocessor(const void* buf,int len,int id)
{
	struct ether_addr src;
	const unsigned char *cbuf=buf;
	for(int i=0;i<6;i++)
		src.ether_addr_octet[i]=cbuf[i+6];
	if(check_mac(src))// This is a loopback packet
		return -1;
	unsigned short typ=*(unsigned short*)(cbuf+12);
	const unsigned char *ip_buf=cbuf+14;
	if(typ==ETH_TYPE_ROUTE)// It'a a routing table
	{
		return update_distance_vector(id,buf,len);
	}
	else if(typ==ETH_TYPE_IP)// It's a IP packet
	{
		cbuf+=14;
		struct in_addr dest;
		dest.s_addr=*(const unsigned int*)(ip_buf+16);
		if(check_ip_addr(dest))// This host is the destination of the packet
		{
			if(global_callback)
			{
				pthread_mutex_lock(&callback_lock);
				int v=(*global_callback)(ip_buf,len-18);
				pthread_mutex_unlock(&callback_lock);
				return v;
			}
		}
		else// We need to route this packet
		{
			return forward_ip_packet(ip_buf,len-18);
		}
	}
	return 0;
}

void *router_thread(void *vargp)
{
	set_frame_receive_callback(packet_preprocessor);
	receive_frame_loop(-1);
	return NULL;
}

void *timer_thread(void *vargp)
{
	while(1)
	{
		tick_routing_table();
		route_local_device();
		broadcast_distance_vector();
		sleep(ROUTE_INTERVAL);
	}
	return NULL;
}

void print_routing_table()
{
	printf("dest|mask|device|mac|hops|timeout\n");
	pthread_mutex_lock(&route_lock);
	for(route_t *p=route_head;p;p=p->next)
	{
		char sdest[INET_ADDRSTRLEN],smask[INET_ADDRSTRLEN],smac[30],name[100];
		inet_ntop(AF_INET,&p->dest,sdest,INET_ADDRSTRLEN);
		inet_ntop(AF_INET,&p->mask,smask,INET_ADDRSTRLEN);
		ether_ntoa_r(&p->mac,smac);
		name[0]='1';
		name[1]=0;
		if(p->id)
			strcpy(name,find_device_id(p->id)->name);
		else
			strcpy(name,"NULL");
		printf("%s|%s|%s|%s|%d|%d\n",sdest,smask,name,smac,p->hops,p->timeout);
	}
	pthread_mutex_unlock(&route_lock);
	printf("\n");
}

int print_packet(const void *buf,int len)
{
	const unsigned char *cbuf=buf;
	printf("Received packet:\n");
	printf("Length: %d\n",len);
	struct in_addr src,dest;
	src.s_addr=*(unsigned int*)(cbuf+12);
	dest.s_addr=*(unsigned int*)(cbuf+16);
	char ssrc[INET_ADDRSTRLEN],sdest[INET_ADDRSTRLEN];
	inet_ntop(AF_INET,&src,ssrc,INET_ADDRSTRLEN);
	inet_ntop(AF_INET,&dest,sdest,INET_ADDRSTRLEN);
	printf("Source: %s\n",ssrc);
	printf("Destination: %s\n",sdest);
	printf("TTL: %d\n",cbuf[8]);
	for(int i=0;i<len;i++)
		printf("%02x ",cbuf[i]);
	printf("\n");
	for(int i=0;i<len;i++)
		printf("%c",cbuf[i]);
	printf("\n");
	return 0;
}