#include"packetio.h"
#include"device.h"
#include<netinet/ether.h>
#include<pcap/pcap.h>
#include<memory.h>
#include<malloc.h>
#include<string.h>
#include<stdio.h>
#include<poll.h>

frame_receive_callback global_callback;

int send_frame(const void *buf,int len,int eth_type,const void *dest_mac,int id)
{
	device_t *dev=find_device_id(id);
	if(dev==NULL)
	{
		fprintf(stderr,"Error: send_frame: device #%d not found\n",id);
		return -1;
	}
	struct ether_addr dest_addr;
	ether_aton_r(dest_mac,&dest_addr);
	unsigned char *frame=calloc(len+18,1);
	for(int i=0;i<6;i++)
	{
		frame[i]=dest_addr.ether_addr_octet[i];
		frame[i+6]=dev->addr.ether_addr_octet[i];
	}
	frame[12]=(eth_type>>8)&0xFF;
	frame[13]=eth_type&0xFF;
	memcpy(frame+14,buf,len);
	if(pcap_sendpacket(dev->handle,frame,len+18))
	{
		fprintf(stderr,"Error: send_frame: device %s send failure\n",dev->name);
		free(frame);
		return -1;
	}
	free(frame);
	return 0;
}

int set_frame_receive_callback(frame_receive_callback callback)
{
	global_callback=callback;
	return 0;
}

void callback_wrapper(u_char *user,const struct pcap_pkthdr *h,const u_char *bytes)
{
	(*global_callback)(bytes,h->caplen,(int)((long long)user));
}

int receive_frame_loop(int cnt)
{
	int inf=(cnt<=0),i,num_dev=0;
	device_t *p;
	for(p=dev_head;p;p=p->next)
		num_dev++;
	struct pollfd *fds=calloc(num_dev,sizeof(struct pollfd));
	int can_poll=1;
	for(p=dev_head,i=0;p;p=p->next,i++)
	{
		if((p->fd=pcap_get_selectable_fd(p->handle))<0)
			can_poll=0;
		fds[i].fd=p->fd;
		fds[i].events=POLLIN;
		fds[i].revents=0;
	}
	if(can_poll) // file descriptor exists, uses poll for i/o multiplexing 
	{
		while(1)
		{
			poll(fds,num_dev,-1);
			for(p=dev_head,i=0;p;p=p->next,i++)
			{
				if(fds[i].revents&POLLIN)
				{
					int num_frame=pcap_dispatch(p->handle,cnt,callback_wrapper,(u_char*)((long long)p->id));
					// printf("num:%d\n",num_frame);
					if(num_frame<0)
					{
						fprintf(stderr,"Error: receive_frame_loop: could not receive from device %s\n",p->name);
						free(fds);
						return -1;
					}
					if(!inf)
					{
						cnt-=num_frame;
						if(cnt<=0)
						{
							free(fds);
							return 0;
						}
					}
				}
			}
		}
	}
	else // file descriptor does not exist, brute force through devices
	{
		free(fds);
		while(1)
		{
			for(p=dev_head;p;p=p->next)
			{
				int num_frame=pcap_dispatch(p->handle,cnt,callback_wrapper,(u_char*)((long long)p->id));
				if(num_frame<0)
				{
					fprintf(stderr,"Error: receive_frame_loop: could not receive from device %s\n",p->name);
					return -1;
				}
				if(!inf)
				{
					cnt-=num_frame;
					if(cnt<=0)
						return 0;
				}
			}
		}
	}
	//shouldn't get here
	return 0;
}