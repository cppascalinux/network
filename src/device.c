#include"device.h"
#include<pcap/pcap.h>
#include<netinet/ether.h>
#include<string.h>
#include<memory.h>
#include<malloc.h>
#include<stdio.h>
#include<unistd.h>
#include<dirent.h>
#define TIMEOUT 100 // default buffer timeout, in miliseconds

// some global variables
static int total_id;
device_t *dev_head;
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
	ether_aton_r(addr,&(dev->addr));
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
	pcap_set_timeout(handle,TIMEOUT);
	if(pcap_activate(handle)<0)
	{
		fprintf(stderr,"Error: add_device: could not activate device %s\n",name);
		free_device(dev);
		return -1;
	}
	dev->handle=handle;
	dev->fd=-1;
	dev->next=dev_head;
	dev_head=dev;
	return dev->id;
}

device_t *find_device_name(const char *name)
{
	device_t *p;
	for(p=dev_head;p;p=p->next)
		if(!strcmp(p->name,name))
			return p;
	return NULL;
}

device_t *find_device_id(int id)
{
	device_t *p;
	for(p=dev_head;p;p=p->next)
		if(p->id==id)
			return p;
	return NULL;
}

device_t *find_device_fd(int fd)
{
	device_t *p;
	for(p=dev_head;p;p=p->next)
		if(p->fd==fd)
			return p;
	return NULL;
}