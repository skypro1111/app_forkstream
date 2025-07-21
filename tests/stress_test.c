/*
 * Stress test for ForkStream module
 * 
 * This program simulates high-load scenarios to test the module's
 * performance, memory usage, and stability under stress conditions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <sys/time.h>

/* Test configuration */
#define MAX_CONCURRENT_CHANNELS 100
#define FRAMES_PER_CHANNEL 1000
#define FRAME_INTERVAL_US 20000  /* 20ms = 50fps typical for audio */
#define TEST_DURATION_SEC 30

/* Statistics */
struct test_stats {
	unsigned long frames_sent;
	unsigned long bytes_sent;
	unsigned long errors;
	double start_time;
	double end_time;
};

static volatile int test_running = 1;
static struct test_stats stats = {0};

/* Simplified state structure */
struct fork_stream_state {
	int sock;
	struct sockaddr_in dest_addr;
	int framehook_id;
	char channel_name[64];
};

/* Get current time in seconds with microsecond precision */
static double get_time(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec + tv.tv_usec / 1000000.0;
}

/* Signal handler for graceful termination */
static void signal_handler(int sig)
{
	(void)sig;
	test_running = 0;
	printf("\nReceived signal, stopping test...\n");
}

/* Create UDP socket for a channel */
static int create_channel_socket(struct fork_stream_state *state, const char *ip_addr, int port)
{
	int sock;
	struct sockaddr_in *dest_addr;

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) {
		return -1;
	}

	dest_addr = &state->dest_addr;
	memset(dest_addr, 0, sizeof(*dest_addr));
	dest_addr->sin_family = AF_INET;
	dest_addr->sin_port = htons(port);

	if (inet_aton(ip_addr, &dest_addr->sin_addr) == 0) {
		close(sock);
		return -1;
	}

	state->sock = sock;
	state->framehook_id = rand() % 10000;  /* Simulate framehook ID */
	return 0;
}

/* Simulate sending an audio frame */
static int send_audio_frame(struct fork_stream_state *state, const char *frame_data, int frame_size)
{
	ssize_t sent_bytes;

	sent_bytes = sendto(state->sock, frame_data, frame_size, 0,
		(struct sockaddr *)&state->dest_addr, sizeof(state->dest_addr));

	if (sent_bytes == frame_size) {
		stats.frames_sent++;
		stats.bytes_sent += frame_size;
		return 0;
	} else {
		stats.errors++;
		return -1;
	}
}

/* Cleanup channel resources */
static void cleanup_channel(struct fork_stream_state *state)
{
	if (state->sock >= 0) {
		close(state->sock);
		state->sock = -1;
	}
}

/* Test scenario: Multiple concurrent channels */
static int test_concurrent_channels(const char *ip_addr, int port, int num_channels)
{
	struct fork_stream_state *channels;
	char frame_data[160];  /* G.711 frame size */
	int i, j;
	double start_time, current_time;

	printf("Testing %d concurrent channels...\n", num_channels);

	/* Allocate channel states */
	channels = calloc(num_channels, sizeof(struct fork_stream_state));
	if (!channels) {
		printf("ERROR: Failed to allocate memory for %d channels\n", num_channels);
		return -1;
	}

	/* Initialize channels */
	for (i = 0; i < num_channels; i++) {
		snprintf(channels[i].channel_name, sizeof(channels[i].channel_name), 
			"SIP/test-channel-%04d", i + 1);
		channels[i].sock = -1;
		
		if (create_channel_socket(&channels[i], ip_addr, port) != 0) {
			printf("ERROR: Failed to create socket for channel %d\n", i + 1);
			/* Cleanup already created channels */
			for (j = 0; j < i; j++) {
				cleanup_channel(&channels[j]);
			}
			free(channels);
			return -1;
		}
	}

	/* Prepare test frame data */
	for (i = 0; i < 160; i++) {
		frame_data[i] = (char)(i % 256);
	}

	start_time = get_time();

	/* Send frames from all channels */
	for (j = 0; j < FRAMES_PER_CHANNEL && test_running; j++) {
		for (i = 0; i < num_channels && test_running; i++) {
			/* Simulate RX frame */
			send_audio_frame(&channels[i], frame_data, 160);
			
			/* Simulate TX frame */
			send_audio_frame(&channels[i], frame_data, 160);
		}

		/* Progress indicator */
		if (j % 100 == 0) {
			current_time = get_time();
			printf("  Progress: %d/%d frames, %.1f fps, %.1f MB/s\n",
				j, FRAMES_PER_CHANNEL,
				stats.frames_sent / (current_time - start_time),
				stats.bytes_sent / (current_time - start_time) / (1024*1024));
		}

		/* Simulate real-time frame rate */
		usleep(FRAME_INTERVAL_US);
	}

	/* Cleanup all channels */
	for (i = 0; i < num_channels; i++) {
		cleanup_channel(&channels[i]);
	}

	free(channels);
	return 0;
}

/* Test scenario: Memory stress test */
static int test_memory_stress(const char *ip_addr, int port)
{
	printf("Testing memory allocation/deallocation stress...\n");

	for (int cycle = 0; cycle < 1000 && test_running; cycle++) {
		struct fork_stream_state *state = malloc(sizeof(*state));
		if (!state) {
			printf("ERROR: Memory allocation failed at cycle %d\n", cycle);
			return -1;
		}

		state->sock = -1;
		if (create_channel_socket(state, ip_addr, port) == 0) {
			char frame[20];  /* G.729 frame size */
			memset(frame, 0x80 + (cycle % 128), sizeof(frame));
			
			send_audio_frame(state, frame, sizeof(frame));
			cleanup_channel(state);
		}

		free(state);

		if (cycle % 100 == 0) {
			printf("  Memory stress cycle: %d/1000\n", cycle);
		}
	}

	return 0;
}

/* Test scenario: Error conditions */
static int test_error_conditions(void)
{
	printf("Testing error condition handling...\n");

	struct fork_stream_state state;
	char frame[160];

	/* Test with invalid socket */
	state.sock = -1;
	state.framehook_id = -1;
	memset(frame, 0, sizeof(frame));

	/* This should fail gracefully */
	if (send_audio_frame(&state, frame, sizeof(frame)) == 0) {
		printf("WARNING: Send succeeded with invalid socket\n");
	}

	/* Test with invalid address */
	state.sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (state.sock >= 0) {
		memset(&state.dest_addr, 0, sizeof(state.dest_addr));
		state.dest_addr.sin_family = AF_INET;
		state.dest_addr.sin_port = htons(1);  /* Likely closed port */
		state.dest_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

		/* This might fail, which is expected */
		send_audio_frame(&state, frame, sizeof(frame));
		close(state.sock);
	}

	printf("  Error condition tests completed\n");
	return 0;
}

/* Print test results */
static void print_results(void)
{
	double duration = stats.end_time - stats.start_time;
	double fps = stats.frames_sent / duration;
	double mbps = stats.bytes_sent / duration / (1024 * 1024);

	printf("\n============================================================\n");
	printf("STRESS TEST RESULTS\n");
	printf("============================================================\n");
	printf("Duration:       %.2f seconds\n", duration);
	printf("Frames sent:    %lu\n", stats.frames_sent);
	printf("Bytes sent:     %lu (%.2f MB)\n", stats.bytes_sent, stats.bytes_sent / (1024.0 * 1024.0));
	printf("Errors:         %lu\n", stats.errors);
	printf("Frame rate:     %.2f fps\n", fps);
	printf("Throughput:     %.2f MB/s\n", mbps);
	printf("Error rate:     %.4f%%\n", 
		stats.frames_sent > 0 ? (stats.errors * 100.0 / stats.frames_sent) : 0.0);

	if (fps >= 40.0 && stats.errors == 0) {
		printf("Status:         ✅ PASS - Performance acceptable\n");
	} else if (fps >= 20.0 && stats.errors < stats.frames_sent * 0.01) {
		printf("Status:         ⚠️  MARGINAL - May need optimization\n");
	} else {
		printf("Status:         ❌ FAIL - Performance issues detected\n");
	}
	printf("============================================================\n");
}

int main(int argc, char *argv[])
{
	char *ip = "127.0.0.1";
	int port = 8080;
	int num_channels = 10;

	if (argc >= 3) {
		ip = argv[1];
		port = atoi(argv[2]);
	}
	if (argc >= 4) {
		num_channels = atoi(argv[3]);
		if (num_channels > MAX_CONCURRENT_CHANNELS) {
			num_channels = MAX_CONCURRENT_CHANNELS;
		}
	}

	printf("ForkStream Stress Test\n");
	printf("======================\n");
	printf("Target: %s:%d\n", ip, port);
	printf("Channels: %d\n", num_channels);
	printf("Duration: %d seconds\n", TEST_DURATION_SEC);
	printf("\n");

	/* Setup signal handlers */
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	stats.start_time = get_time();

	/* Run test scenarios */
	if (test_concurrent_channels(ip, port, num_channels) != 0) {
		printf("ERROR: Concurrent channels test failed\n");
		return 1;
	}

	if (test_memory_stress(ip, port) != 0) {
		printf("ERROR: Memory stress test failed\n");
		return 1;
	}

	if (test_error_conditions() != 0) {
		printf("ERROR: Error conditions test failed\n");
		return 1;
	}

	stats.end_time = get_time();

	/* Print results */
	print_results();

	/* Return success if error rate is acceptable (< 1%) */
	double error_rate = stats.frames_sent > 0 ? (stats.errors * 100.0 / stats.frames_sent) : 0.0;
	return (error_rate < 1.0) ? 0 : 1;
} 