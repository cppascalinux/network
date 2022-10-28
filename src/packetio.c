#include"packetio.h"
#include"device.h"
#include"ip.h"
#include<netinet/ether.h>
#include<pcap/pcap.h>
#include<memory.h>
#include<malloc.h>
#include<string.h>
#include<stdio.h>
#include<poll.h>
#include<pthread.h>
#include<unistd.h>

static frame_receive_callback global_callback;
static pthread_mutex_t callback_lock=PTHREAD_MUTEX_INITIALIZER;

int send_frame(const void *buf,int len,int eth_type,const struct ether_addr dest_mac,int id)
{
	device_t *dev=find_device_id(id);
	if(dev==NULL)
	{
		fprintf(stderr,"Error: send_frame: device #%d not found\n",id);
		return -1;
	}
	// printf("send_frame: id:%d\n",id);
	unsigned char *frame=calloc(len+14,1);
	for(int i=0;i<6;i++)
	{
		frame[i]=dest_mac.ether_addr_octet[i];
		frame[i+6]=dev->addr.ether_addr_octet[i];
	}
	frame[12]=eth_type&0xFF;
	frame[13]=(eth_type>>8)&0xFF;
	memcpy(frame+14,buf,len);
	int ret=0;
	pthread_mutex_lock(&dev->lock);
	if(pcap_sendpacket(dev->handle,frame,len+14))
	{
		fprintf(stderr,"Error: send_frame: device %s send failure\n",dev->name);
		ret=-1;
	}
	pthread_mutex_unlock(&dev->lock);
	free(frame);
	return ret;
}

int broadcast_frame(const void *buf,int len,int eth_type,const struct ether_addr dest_mac)
{
	pthread_mutex_lock(&dev_lock);
	int i=0;
	for(device_t *p=dev_head;p;p=p->next)
		i++;
	int tot=i;
	// printf("broadcast_frame: tot: %d\n",tot);
	int *lid=calloc(tot,sizeof(int));
	i=0;
	for(device_t *p=dev_head;p;p=p->next)
		lid[i++]=p->id;
	pthread_mutex_unlock(&dev_lock);
	int ret=0;
	for(i=0;i<tot;i++)
		if(send_frame(buf,len,eth_type,dest_mac,lid[i])<0)
			ret=-1;
	free(lid);
	return ret;
}

int set_frame_receive_callback(frame_receive_callback callback)
{
	pthread_mutex_lock(&callback_lock);
	global_callback=callback;
	pthread_mutex_unlock(&callback_lock);
	return 0;
}

void callback_wrapper(u_char *user,const struct pcap_pkthdr *h,const u_char *bytes)
{
	pthread_mutex_lock(&callback_lock);
	(*global_callback)(bytes,h->caplen,(int)((long long)user));
	pthread_mutex_unlock(&callback_lock);
}

int receive_frame_loop(int cnt)
{
	int inf=(cnt<=0);
	while(1)
	{
		// first, make a copy of the device list
		pthread_mutex_lock(&dev_lock);
		int num_dev=0,i=0;
		for(device_t *p=dev_head;p;p=p->next)
			num_dev++;
		if(num_dev==0)// no device added yet
		{
			pthread_mutex_unlock(&dev_lock);
			usleep(BUF_TIMEOUT);// sleep for 0.1 seconds
			continue;
		}
		device_t **ar_dev=calloc(num_dev,sizeof(device_t*));
		for(device_t *p=dev_head;p;p=p->next)
			ar_dev[i++]=p;
		pthread_mutex_unlock(&dev_lock);
		// then create the file descriptor list
		struct pollfd *fds=calloc(num_dev,sizeof(struct pollfd));
		for(i=0;i<num_dev;i++)
		{
			fds[i].fd=ar_dev[i]->fd;
			fds[i].events=POLLIN;
			fds[i].revents=0;
		}
		// wait for IO
		poll(fds,num_dev,BUF_TIMEOUT);
		for(int i=0;i<num_dev;i++)
		{
			if(fds[i].revents&POLLIN)
			{
				// pthread_mutex_lock(&ar_dev[i]->lock);
				int num_frame=pcap_dispatch(ar_dev[i]->handle,cnt,callback_wrapper,(unsigned char*)((long long)ar_dev[i]->id));
				// pthread_mutex_unlock(&ar_dev[i]->lock);
				if(num_frame<0)
				{
					fprintf(stderr,"Error: receive_frame_loop: could not receive from device %s\n",ar_dev[i]->name);
					free(fds);
					free(ar_dev);
					return -1;
				}
				if(!inf)
				{
					cnt-=num_frame;
					if(cnt<=0)
					{
						free(fds);
						free(ar_dev);
						return 0;
					}
				}
			}
		}
		free(fds);
		free(ar_dev);
	}
}
