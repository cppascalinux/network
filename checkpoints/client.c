#include"../src/device.h"
#include"../src/packetio.h"
#include"../src/ip.h"
#include<pthread.h>
#include<malloc.h>
#include<string.h>
#include<stdio.h>
#include<netinet/ip.h>
#include<netinet/ether.h>

int main()
{
	pthread_t router,timer;
	pthread_create(&router,NULL,router_thread,NULL);
	pthread_create(&timer,NULL,timer_thread,NULL);
	set_ip_packet_receive_callback(print_packet);
	char cmd[100];
	while(1)
	{
		scanf("%s",cmd);
		switch(cmd[0])
		{
			case 'l':
				char **s=get_available_device();
				if(!s)
					printf("Failed to get available devices\n");
				else
				{
					for(int i=0;s[i];i++)
						printf("%s\n",s[i]);
					free_device_list(s);
				}
				break;
				
			case 'a': // add device
				char name[100];
				scanf("%s",name);
				int v=add_device(name);
				if(v>0)
					printf("Device added successfully with id: #%d\n",v);
				else
					printf("Failed to add device\n");
				break;
			
			case 't':// add all devices
				int num=add_all_devices();
				printf("Successfully added %d devices\n",num);
				break;

			case 's':
				char ssrc[INET_ADDRSTRLEN],sdst[INET_ADDRSTRLEN],msg[100];
				scanf("%s%s%s",ssrc,sdst,msg);
				struct in_addr src,dest;
				inet_pton(AF_INET,ssrc,&src);
				inet_pton(AF_INET,sdst,&dest);
				if(send_ip_packet(src,dest,0,msg,strlen(msg)))
					fprintf(stderr,"Error: failed to send packet\n");
				break;
			
			case 'p':
				print_routing_table();
				break;
			
			case 'd':
				print_device_table();
				break;
			
			case 'r':
				char smask[INET_ADDRSTRLEN],smac[30];
				int id,hops,timeout;
				scanf("%s%s%d%s%d%d",sdst,smask,&id,smac,&hops,&timeout);
				struct in_addr mask;
				struct ether_addr mac;
				inet_pton(AF_INET,sdst,&dest);
				inet_pton(AF_INET,smask,&mask);
				ether_aton_r(smac,&mac);
				update_routing_entry(dest,mask,id,mac,hops,timeout);
		}
	}
	return 0;
}