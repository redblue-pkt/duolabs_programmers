#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <linux/input.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#include "../dynamite/dynamite_ioctl.h"

#include "dynamite_control.h"

typedef struct
{
	char *arg;
	char *arg_long;
	char *arg_description;
} tArgs;

tArgs vArgs[] =
{
	{ "-c", " --setCardprogrammer  ", "Args: No argumens\n\tSet card programmer mode" },
	{ "-p", " --setPhoenix	", "Args: 357, 368, 400, 600\n\tSet phoenix mode" },
	{ "-s", " --setSmartmouse	", "Args: 357, 368, 400, 600\n\tSet smartmouse mode" },
	{ NULL, NULL, NULL }
};

void usage(char *prg, char *cmd)
{
	int i;

	struct dynamite_device_information_command dynamite_info_cmd;

	FILE *fdynamite = fopen(DYNAMITE_DEVICE, "r");

	/* or printout a default usage */
	fprintf(stderr, "Dynamite Programmer control tool, version 1.00\n");
	if (fdynamite)
	{
		fd = open(DYNAMITE_DEVICE, O_RDWR);
		if (fd < 0)
		{
			fprintf(stderr, "Failed open device: %s\n", DYNAMITE_DEVICE);
			exit(1);
		}
		char device_name[64];
		if (ioctl(fd, IOCTL_DEVICE_INFORMATION_COMMAND, &dynamite_info_cmd) < 0)
		{
			fprintf(stderr, "Failed send ioctl command: SET_CARDPROGRAMMER, (%m)\n");
			exit(1);
		}
		fprintf(stderr, "Found device: %s, vid: 0x%04x, pid: 0x%04x, status: %s \n", dynamite_device_list[dynamite_info_cmd.device], dynamite_info_cmd.vid, dynamite_info_cmd.pid, dynamite_device_status[dynamite_info_cmd.status]);
	}
	else
	{
		fprintf(stderr, "Device not found.\n");
		exit(1);
	}
	fprintf(stderr, "\n");
	fprintf(stderr, "General usage:\n\n");
	fprintf(stderr, "%s argument [optarg1] [optarg2]\n", prg);

	for (i = 0; ; i++)
	{
		if (vArgs[i].arg == NULL)
			break;
		if ((cmd == NULL) || (strcmp(cmd, vArgs[i].arg) == 0) || (strstr(vArgs[i].arg_long, cmd) != NULL))
			fprintf(stderr, "%s %s %s\n", vArgs[i].arg, vArgs[i].arg_long, vArgs[i].arg_description);
	}
	exit(1);
}

int main(int argc, char *argv[])
{
	int i;
	if (argc > 1)
	{
		i = 1;
		while (i < argc)
		{
			if ((strcmp(argv[i], "-c") == 0) || (strcmp(argv[i], "--setCardprogrammer") == 0))
			{
				fd = open(DYNAMITE_DEVICE, O_RDWR);
				if (fd < 0)
				{
					fprintf(stderr, "Failed open device: %s\n", DYNAMITE_DEVICE);
					exit(1);
				}
				if (ioctl(fd, IOCTL_SET_CARDPROGRAMMER) < 0)
				{
					fprintf(stderr, "Failed send ioctl command: SET_CARDPROGRAMMER, (%m)\n");
					exit(1);
				}
			}
			else if ((strcmp(argv[i], "-p") == 0) || (strcmp(argv[i], "--setPhoenix") == 0))
			{
				if (i + 1 <= argc)
				{
					int pmhz;
					if (argv[i + 1] == NULL)
					{
						fprintf(stderr, "Missing mhz value\n");
						usage(argv[0], NULL);
					}
					pmhz = atoi(argv[i + 1]);
					if (pmhz != 357 && pmhz != 368 && pmhz != 400 && pmhz != 600)
					{
						fprintf(stderr, "Mhz value out of range\n");
                                        	usage(argv[0], NULL);
					}
					fd = open(DYNAMITE_DEVICE, O_RDWR);
					if (fd < 0)
					{
						fprintf(stderr, "Failed open device: %s\n", DYNAMITE_DEVICE);
						exit(1);
					}
					switch(pmhz)
					{
						case 357:
							if (ioctl(fd, IOCTL_SET_PHOENIX_357) < 0)
								fprintf(stderr, "Failed send ioctl command: IOCTL_SET_PHOENIX_357, (%m)\n");
							break;
						case 368:
							if (ioctl(fd, IOCTL_SET_PHOENIX_368) < 0)
								fprintf(stderr, "Failed send ioctl command: IOCTL_SET_PHOENIX_368, (%m)\n");
							break;
						case 400:
							if (ioctl(fd, IOCTL_SET_PHOENIX_400) < 0)
								fprintf(stderr, "Failed send ioctl command: IOCTL_SET_PHOENIX_400, (%m)\n");
							break;
						case 600:
							if (ioctl(fd, IOCTL_SET_PHOENIX_600) < 0)
								fprintf(stderr, "Failed send ioctl command: IOCTL_SET_PHOENIX_600, (%m)\n");
							break;
						default:
							break;
					}
				}
				i += 1;
			}
			else if ((strcmp(argv[i], "-s") == 0) || (strcmp(argv[i], "--setSmartmouse") == 0))
			{
				if (i + 1 <= argc)
				{
					int smhz;
					if (argv[i + 1] == NULL)
					{
						fprintf(stderr, "Missing mhz value\n");
						usage(argv[0], NULL);
					}
					smhz = atoi(argv[i + 1]);
					if (smhz != 357 && smhz != 368 && smhz != 400 && smhz != 600)
					{
						fprintf(stderr, "Mhz value out of range\n");
						usage(argv[0], NULL);
					}
					fd = open(DYNAMITE_DEVICE, O_RDWR);
					if (fd < 0)
					{
						fprintf(stderr, "Failed open device: %s\n", DYNAMITE_DEVICE);
						exit(1);
					}
					switch(smhz)
					{
						case 357:
							if (ioctl(fd, IOCTL_SET_SMARTMOUSE_357) < 0)
								fprintf(stderr, "Failed send ioctl command: IOCTL_SET_SMARTMOUSE_357, (%m)\n");
							break;
						case 368:
							if (ioctl(fd, IOCTL_SET_SMARTMOUSE_368) < 0)
								fprintf(stderr, "Failed send ioctl command: IOCTL_SET_SMARTMOUSE_368, (%m)\n");
							break;
						case 400:
							if (ioctl(fd, IOCTL_SET_SMARTMOUSE_400) < 0)
								fprintf(stderr, "Failed send ioctl command: IOCTL_SET_SMARTMOUSE_400, (%m)\n");
							break;
						case 600:
							if (ioctl(fd, IOCTL_SET_SMARTMOUSE_600) < 0)
								fprintf(stderr, "Failed send ioctl command: IOCTL_SET_SMARTMOUSE_600, (%m)\n");
							break;
						default:
							break;
					}
				}
				i += 1;
			}
			else
			{
				usage(argv[0], NULL);
			}
			i++;
		}
	}
	else
	{
		usage(argv[0], NULL);
	}
	close(fd);
	fd = -1;
	return 0;
}
