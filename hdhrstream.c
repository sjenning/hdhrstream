#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <signal.h>

#include "hdhomerun.h"

#define PKTSZ VIDEO_DATA_PACKET_SIZE

static int stop;

void sig_handler(int sig)
{
	printf("stopping\n");
	stop = 1;
}

int main(int argc, char **argv)
{
	struct hdhomerun_device_t *hd;
	struct hdhomerun_tuner_status_t status;
	int s, sndbufsz;
	size_t rcvbufsz, rcvsz;
	struct sockaddr_in dst;
	uint8_t *buf, *end;
	struct sigaction sa;
	
	hd = hdhomerun_device_create(HDHOMERUN_DEVICE_ID_WILDCARD, 0, 0, NULL);
	if (!hd) {
		printf("hdhomerun_device_create");
	}

	if (hdhomerun_device_set_tuner_channel(hd, "auto:7") != 1) {
		printf("hdhomerun_device_set_tuner_channel");
		goto destroy;
	}

	if (hdhomerun_device_set_tuner_program(hd, "3") != 1) {
		printf("hdhomerun_device_set_tuner_channel");
		goto destroy;
	}

	hdhomerun_device_wait_for_lock(hd, &status);

	/* set up udp relay socket */
	if((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("socket");
		goto destroy;
	}

	sndbufsz = 1024 * 1024;
	if (setsockopt(s, SOL_SOCKET, SO_SNDBUF,
			&sndbufsz, sizeof(sndbufsz)) < 0) {
		perror("setsockopt");
		goto close;
	}

	memset(&dst, 0, sizeof(dst));
	dst.sin_family = AF_INET;
	inet_pton(AF_INET, "192.168.1.173", &dst.sin_addr);
	dst.sin_port = htons(5000);

	/* register signal handler */
	sa.sa_handler = sig_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);

	/* tell hdhomerun to start streaming */
	if(hdhomerun_device_stream_start(hd) != 1) {
		printf("hdhomerun_device_stream_start");
		goto destroy;
	}

	printf("ctrl-c to exit\n");

	rcvbufsz = PKTSZ * 32;

	while (!stop) {
		buf = hdhomerun_device_stream_recv(hd, rcvbufsz, &rcvsz);
		if (!buf) {
			usleep(15000);
			continue;
		}
		end = buf + rcvsz;
		while (buf + PKTSZ <= end) {
			printf(".");
			fflush(stdout);
			if (sendto(s, buf, PKTSZ, 0, (struct sockaddr *)&dst, sizeof(dst)) != PKTSZ) {
				perror("sendto");
				goto stop;
			}
			buf += PKTSZ;
		}
	}

stop:
	hdhomerun_device_stream_flush(hd);
	hdhomerun_device_stream_stop(hd);
close:
	close(s);
destroy:
	hdhomerun_device_destroy(hd);

	return 0;
}
