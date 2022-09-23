#include"device.h"
#include"packetio.h"
#include<string.h>
#include<stdio.h>
#define DISPLAY_LENG 48
#define TYPE_IP 0x800


static char *msg="This is a test message qwq";
static char *dest_addr="ff:ff:ff:ff:ff:ff";
static int cnt_packet;
int callback(const void *buf,int len,int id)
{
	printf("No.%d length: %d device id: #%d device name: %s\n",++cnt_packet,len,id,find_device_id(id)->name);
	for(int i=0;i<len&&i<DISPLAY_LENG;i++)
		printf("%02x ",((unsigned char*)buf)[i]);
	printf("\n");
	for(int i=0;i<len&&i<DISPLAY_LENG;i++)
		putchar(((unsigned char*)buf)[i]);
	printf("\n");
	return 0;
}

int main()
{
	char cmd[257],name[257];
	while(1)
	{
		scanf("%s",cmd);
		switch(cmd[0])
		{
			case 'l': // get list of available network devices
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
				scanf("%s",name);
				int v=add_device(name);
				if(v>0)
					printf("Device added successfully with id: #%d\n",v);
				else
					printf("Failed to add device\n");
				break;

			case 's': // send frames
				int id;
				scanf("%d",&id);
				if(send_frame(msg,strlen(msg),TYPE_IP,dest_addr,id))
					printf("Failed to send frame\n");
				else
					printf("Frame sent successfully\n");
				break;

			case 'r': // receive frames
				cnt_packet=0;
				set_frame_receive_callback(callback);
				int cnt;
				scanf("%d",&cnt);
				printf("Receiving %d frames\n",cnt);
				receive_frame_loop(cnt);
				printf("Received %d frames\n",cnt);
				break;

			default:
				printf("Unknown command: %c\n",cmd[0]);
		}
	}
	return 0;
}