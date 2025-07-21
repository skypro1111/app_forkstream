/*
 * Test program for ForkStream framehook functionality
 * 
 * This program simulates the framehook callback behavior
 * to test UDP packet sending without requiring full Asterisk.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

/* Simplified state structure */
struct fork_stream_state {
	int sock;
	struct sockaddr_in dest_addr;
	int framehook_id;
};

/* Simulate audio frame data */
struct simulated_frame {
	char *data;
	int datalen;
	const char *direction;
};

/* Test function that simulates the framehook callback */
static int test_send_frame(struct fork_stream_state *state, struct simulated_frame *frame)
{
	ssize_t sent_bytes;

	printf("Processing %s frame: %d bytes\n", frame->direction, frame->datalen);

	/* Send the audio data via UDP (same logic as in real module) */
	sent_bytes = sendto(state->sock, frame->data, frame->datalen, 0,
		(struct sockaddr *)&state->dest_addr, sizeof(state->dest_addr));

	if (sent_bytes < 0) {
		printf("ERROR: Failed to send %s frame: %s\n", frame->direction, strerror(errno));
		return -1;
	} else if (sent_bytes != frame->datalen) {
		printf("WARNING: Partial %s frame send: sent %zd of %d bytes\n",
			frame->direction, sent_bytes, frame->datalen);
		return -1;
	} else {
		printf("SUCCESS: Sent %s frame: %d bytes\n", frame->direction, frame->datalen);
		return 0;
	}
}

/* Create UDP socket (same logic as in real module) */
static int create_udp_socket(struct fork_stream_state *state, const char *ip_addr, int port)
{
	int sock;
	struct sockaddr_in *dest_addr;

	/* Create UDP socket */
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) {
		printf("ERROR: Failed to create UDP socket: %s\n", strerror(errno));
		return -1;
	}

	/* Setup destination address */
	dest_addr = &state->dest_addr;
	memset(dest_addr, 0, sizeof(*dest_addr));
	dest_addr->sin_family = AF_INET;
	dest_addr->sin_port = htons(port);

	if (inet_aton(ip_addr, &dest_addr->sin_addr) == 0) {
		printf("ERROR: Failed to convert IP address '%s'\n", ip_addr);
		close(sock);
		return -1;
	}

	state->sock = sock;
	printf("Created UDP socket, destination %s:%d\n", ip_addr, port);
	return 0;
}

int main(int argc, char *argv[])
{
	struct fork_stream_state state;
	char *ip = "127.0.0.1";
	int port = 8080;
	
	if (argc == 3) {
		ip = argv[1];
		port = atoi(argv[2]);
	}

	printf("ForkStream Framehook Test\n");
	printf("========================\n");
	printf("Target: %s:%d\n\n", ip, port);

	/* Initialize state */
	memset(&state, 0, sizeof(state));

	/* Create UDP socket */
	if (create_udp_socket(&state, ip, port) != 0) {
		return 1;
	}

	/* Simulate different types of audio frames */
	struct simulated_frame frames[] = {
		{"Hello from RX", 14, "RX"},  /* Simulated incoming audio */
		{"Hello from TX", 14, "TX"},  /* Simulated outgoing audio */
		/* Simulate typical G.711 frame (160 bytes for 20ms at 8kHz) */
		{NULL, 160, "RX"},
		{NULL, 160, "TX"},
		/* Simulate typical G.729 frame */
		{NULL, 20, "RX"},
		{NULL, 20, "TX"}
	};

	/* Allocate data for larger frames */
	char g711_data[160];
	char g729_data[20];
	
	/* Fill with simulated audio data patterns */
	for (int i = 0; i < 160; i++) {
		g711_data[i] = (char)(i % 256);  /* Simple pattern */
	}
	for (int i = 0; i < 20; i++) {
		g729_data[i] = (char)(0x80 + (i % 128));  /* G.729-like pattern */
	}
	
	frames[2].data = g711_data;
	frames[3].data = g711_data;
	frames[4].data = g729_data;
	frames[5].data = g729_data;

	int num_frames = sizeof(frames) / sizeof(frames[0]);
	int success = 0;

	/* Test each frame */
	for (int i = 0; i < num_frames; i++) {
		printf("\n--- Test Frame %d ---\n", i + 1);
		if (test_send_frame(&state, &frames[i]) == 0) {
			success++;
		}
		usleep(100000);  /* 100ms delay between frames */
	}

	printf("\n========================\n");
	printf("Test Results: %d/%d frames sent successfully\n", success, num_frames);

	/* Cleanup */
	close(state.sock);
	
	return (success == num_frames) ? 0 : 1;
} 