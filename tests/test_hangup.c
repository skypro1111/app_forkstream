/*
 * Test program for ForkStream hangup handler functionality
 * 
 * This program simulates the hangup handler behavior
 * to test resource cleanup without requiring full Asterisk.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

/* Simplified state structure matching the real module */
struct fork_stream_state {
	int sock;
	struct sockaddr_in dest_addr;
	int framehook_id;
};

/* Simulate the hangup handler function */
static void fork_stream_hangup_handler(const char *chan_name, struct fork_stream_state *state)
{
	if (!state) {
		printf("WARNING: Hangup handler called with NULL state on channel %s\n", chan_name);
		return;
	}

	printf("Cleaning up resources for channel %s\n", chan_name);

	/* Simulate framehook detachment */
	if (state->framehook_id >= 0) {
		printf("  Detached framehook (ID: %d) from channel %s\n", 
			state->framehook_id, chan_name);
		state->framehook_id = -1;
	}

	/* Close UDP socket */
	if (state->sock >= 0) {
		close(state->sock);
		printf("  Closed UDP socket for channel %s\n", chan_name);
		state->sock = -1;
	}

	/* Free the state structure */
	free(state);
	printf("Resource cleanup completed for channel %s\n", chan_name);
}

/* Simulate socket creation */
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
	printf("Created UDP socket, destination %s:%d (socket fd: %d)\n", ip_addr, port, sock);
	return 0;
}

/* Simulate the complete ForkStream lifecycle */
static int test_forkstream_lifecycle(const char *chan_name, const char *ip_addr, int port)
{
	struct fork_stream_state *state;

	printf("\n--- Testing ForkStream lifecycle for channel %s ---\n", chan_name);

	/* Allocate state structure (simulate ast_calloc) */
	state = malloc(sizeof(*state));
	if (!state) {
		printf("ERROR: Failed to allocate state structure\n");
		return -1;
	}

	/* Initialize state fields */
	state->sock = -1;
	state->framehook_id = -1;

	/* Create UDP socket */
	if (create_udp_socket(state, ip_addr, port) != 0) {
		free(state);
		return -1;
	}

	/* Simulate framehook attachment */
	state->framehook_id = 12345;  /* Simulated framehook ID */
	printf("Attached framehook (ID: %d) to channel %s\n", state->framehook_id, chan_name);

	/* Simulate hangup handler registration */
	printf("Registered hangup handler for channel %s\n", chan_name);

	printf("ForkStream successfully initialized for %s:%d on channel %s\n", 
		ip_addr, port, chan_name);

	/* Simulate some activity... */
	printf("Channel %s is active, processing audio frames...\n", chan_name);

	/* Simulate channel hangup after some time */
	printf("\nChannel %s is hanging up, triggering hangup handler...\n", chan_name);
	fork_stream_hangup_handler(chan_name, state);

	/* Note: state is freed by hangup handler, don't access it after this point */

	return 0;
}

/* Test error cases */
static void test_error_cases(void)
{
	printf("\n--- Testing error cases ---\n");

	/* Test hangup handler with NULL state */
	fork_stream_hangup_handler("Test-Channel-NULL", NULL);

	/* Test state with invalid socket */
	struct fork_stream_state *state = malloc(sizeof(*state));
	state->sock = -1;
	state->framehook_id = -1;
	printf("\nTesting cleanup with invalid socket:\n");
	fork_stream_hangup_handler("Test-Channel-Invalid", state);
}

int main(int argc, char *argv[])
{
	char *ip = "127.0.0.1";
	int port = 8080;
	
	if (argc == 3) {
		ip = argv[1];
		port = atoi(argv[2]);
	}

	printf("ForkStream Hangup Handler Test\n");
	printf("==============================\n");
	printf("Target: %s:%d\n", ip, port);

	/* Test multiple channel lifecycles */
	const char *test_channels[] = {
		"SIP/alice-00000001",
		"SIP/bob-00000002", 
		"IAX2/charlie-00000003"
	};

	int num_channels = sizeof(test_channels) / sizeof(test_channels[0]);
	int success = 0;

	for (int i = 0; i < num_channels; i++) {
		if (test_forkstream_lifecycle(test_channels[i], ip, port) == 0) {
			success++;
		}
		sleep(1);  /* Brief pause between tests */
	}

	/* Test error scenarios */
	test_error_cases();

	printf("\n==============================\n");
	printf("Test Results: %d/%d channels processed successfully\n", success, num_channels);
	printf("Resource management test completed.\n");

	return (success == num_channels) ? 0 : 1;
} 