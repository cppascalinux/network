#include"device.h"
#include<sys/types.h>
#include<pcap/pcap.h>
#include<netinet/ether.h>
#include<netinet/ip.h>
#include<ifaddrs.h>
#include<string.h>
#include<memory.h>
#include<malloc.h>
#include<stdio.h>
#include<unistd.h>
#include<dirent.h>

static int total_id;
device_t *dev_head;
pthread_mutex_t dev_lock=PTHREAD_MUTEX_INITIALIZER;
char errbuf[PCAP_ERRBUF_SIZE];

char **get_available_device()
{
	DIR *d;
	struct dirent *dir;
	int cnt=0;
	d=opendir("/sys/class/net");
	if(d)
	{
		while((dir=readdir(d)))
			if(strcmp(dir->d_name,".")&&strcmp(dir->d_name,".."))
				cnt++;
		closedir(d);
	}
	else
	{
		fprintf(stderr,"Error: cannot read /sys/class/net\n");
		return NULL;
	}
	if(!cnt)
		return NULL;
	char **s=calloc(cnt+1,sizeof(char*));
	cnt=0;
	d=opendir("/sys/class/net");
	if(d)
	{
		while((dir=readdir(d)))
			if(strcmp(dir->d_name,".")&&strcmp(dir->d_name,".."))
			{
				s[cnt]=calloc(strlen(dir->d_name)+1,1);
				strcpy(s[cnt],dir->d_name);
				cnt++;
			}
		closedir(d);
	}
	else
	{
		fprintf(stderr,"Error: cannot read /sys/class/net\n");
		return NULL;
	}
	return s;
}

void free_device_list(char **s)
{
	if(!s)
		return;
	for(int i=0;s[i];i++)
		free(s[i]);
	free(s);
}

void free_device(device_t *dev)
{
	if(!dev)
		return;
	if(dev->name)
		free(dev->name);
	free(dev);
}

int add_device(const char *name)
{
	char *path=malloc(strlen(name)+30);
	sprintf(path,"/sys/class/net/%s/address",name);
	if(access(path,R_OK))
	{
		fprintf(stderr,"Error: add_device: could not access device %s\n",name);
		free(path);
		return -1;
	}
	if(find_device_name(name))
	{
		fprintf(stderr,"Error: add_device: device %s already added\n",name);
		free(path);
		return -1;
	}
	device_t *dev=malloc(sizeof(device_t));
	dev->name=malloc(strlen(name)+1);
	dev->id=++total_id;
	strcpy(dev->name,name);
	FILE *fp;
	if((fp=fopen(path,"r"))==NULL)
	{
		fprintf(stderr,"Error: add_device: could not open %s\n",path);
		free_device(dev);
		free(path);
		return -1;
	}
	free(path);
	char addr[20];
	fgets(addr,19,fp);
	fclose(fp);
	ether_aton_r(addr,&dev->addr);
	struct ifaddrs *ifap;
	if(getifaddrs(&ifap)<0)
	{
		fprintf(stderr,"Error: add_device: could not get ipv4 address of device %s\n",name);
		free_device(dev);
		return -1;
	}
	int suc=0;
	for(struct ifaddrs *p=ifap;p;p=p->ifa_next)
		if(p->ifa_addr&&p->ifa_addr->sa_family==AF_INET&&!strcmp(p->ifa_name,dev->name))
		{
			suc=1;
			struct sockaddr_in *q=(struct sockaddr_in*)p->ifa_addr;
			dev->ip_addr=q->sin_addr;
		}
	freeifaddrs(ifap);
	if(!suc)
	{
		fprintf(stderr,"Error: add_device: could not get ipv4 address of device %s\n",name);
		free_device(dev);
		return -1;
	}
	pcap_t *handle;
	if((handle=pcap_create(name,errbuf))==NULL)
	{
		fprintf(stderr,"Error: add_device: could not create device %s\n",name);
		free_device(dev);
		return -1;
	}
	if(pcap_setnonblock(handle,1,errbuf))
	{
		fprintf(stderr,"Error: add_device: could not set device %s to unblocking mode\n",name);
		free_device(dev);
		return -1;
	}
	pcap_set_timeout(handle,BUF_TIMEOUT);
	if(pcap_activate(handle)<0)
	{
		fprintf(stderr,"Error: add_device: could not activate device %s\n",name);
		free_device(dev);
		return -1;
	}
	dev->fd=pcap_get_selectable_fd(handle);
	pthread_mutex_init(&dev->lock,NULL);
	dev->handle=handle;
	pthread_mutex_lock(&dev_lock);
	dev->next=dev_head;
	dev_head=dev;
	pthread_mutex_unlock(&dev_lock);
	return dev->id;
}

device_t *find_device_name(const char *name)
{
	device_t *p;
	pthread_mutex_lock(&dev_lock);
	for(p=dev_head;p;p=p->next)
		if(!strcmp(p->name,name))
		{
			pthread_mutex_unlock(&dev_lock);
			return p;
		}
	pthread_mutex_unlock(&dev_lock);
	return NULL;
}

device_t *find_device_id(int id)
{
	device_t *p;
	pthread_mutex_lock(&dev_lock);
	for(p=dev_head;p;p=p->next)
		if(p->id==id)
		{
			pthread_mutex_unlock(&dev_lock);
			return p;
		}
	pthread_mutex_unlock(&dev_lock);
	return NULL;
}

device_t *find_device_fd(int fd)
{
	device_t *p;
	pthread_mutex_lock(&dev_lock);
	for(p=dev_head;p;p=p->next)
		if(p->fd==fd)
		{
			pthread_mutex_unlock(&dev_lock);
			return p;
		}
	pthread_mutex_unlock(&dev_lock);
	return NULL;
}

void print_device_table()
{
	printf("id|name|fd|mac|ip\n");
	pthread_mutex_lock(&dev_lock);
	for(device_t *p=dev_head;p;p=p->next)
	{
		char smac[30],sip[INET_ADDRSTRLEN];
		ether_ntoa_r(&p->addr,smac);
		inet_ntop(AF_INET,&p->ip_addr,sip,INET_ADDRSTRLEN);
		printf("%d|%s|%d|%s|%s\n",p->id,p->name,p->fd,smac,sip);
	}
	printf("\n");
	pthread_mutex_unlock(&dev_lock);
}