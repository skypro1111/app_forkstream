/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2023, skravchenko@stssrv.com
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 *
 * \brief ForkStream dialplan application
 *
 * \author skravchenko@stssrv.com
 *
 * \ingroup applications
 */

#include "asterisk.h"

#include "asterisk/app.h"
#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/logger.h"
#include "asterisk/strings.h"	/* For ast_strdupa and string manipulation */
#include "asterisk/utils.h"		/* For ast_calloc and ast_free */
#include "asterisk/frame.h"		/* For ast_frame structures and types */
#include "asterisk/framehook.h"	/* For framehook functionality */
#include "asterisk/cli.h"		/* For CLI command registration */

#include <sys/socket.h>		/* For socket creation */
#include <sys/uio.h>		/* For sendmsg and iovec */
#include <netinet/in.h>		/* For sockaddr_in structure */
#include <arpa/inet.h>		/* For inet_aton */
#include <unistd.h>			/* For close() */
#include <time.h>			/* For time() and timestamp generation */
#include <errno.h>			/* For errno and strerror */
#include <string.h>			/* For memset and string functions */

/*** DOCUMENTATION
	<application name="ForkStream" language="en_US">
		<synopsis>
			Fork a channel's bidirectional audio stream to a UDP address
		</synopsis>
		<syntax>
			<parameter name="destination" required="true">
				<para>The destination in the format <literal>ip:port</literal></para>
			</parameter>
		</syntax>
		<description>
			<para>This application forks both the read (RX) and write (TX) audio streams
			from a channel to the specified UDP destination. The application is non-blocking
			and returns control to the dialplan immediately after setup.</para>
			<para>The audio data is sent as raw payload without additional headers, using
			the same codec that is active on the channel.</para>
		</description>
	</application>
 ***/

static const char app[] = "ForkStream";

/*! \brief Global logging state */
static int forkstream_logging_enabled = 0;

/*! \brief Packet types for TLV protocol */
#define PACKET_TYPE_SIGNALING  0x01
#define PACKET_TYPE_AUDIO      0x02

/*! \brief Direction flags */
#define DIRECTION_RX           0x01
#define DIRECTION_TX           0x02

/*! \brief TLV packet header (8 bytes) */
struct forkstream_header {
	uint8_t  packet_type;    /*!< Type: 0x01=signaling, 0x02=audio */
	uint16_t packet_length;  /*!< Total packet length including header */
	uint32_t stream_id;      /*!< Unique stream identifier */
	uint8_t  direction;      /*!< Direction: 0x01=RX, 0x02=TX */
} __attribute__((packed));

/*! \brief Signaling packet payload */
struct signaling_payload {
	char channel_id[64];     /*!< Channel identifier */
	char exten[32];          /*!< Extension number */
	char caller_id[32];      /*!< Caller ID */
	char called_id[32];      /*!< Called ID */
	uint32_t timestamp;      /*!< Unix timestamp */
} __attribute__((packed));

/*! \brief Audio packet payload header */
struct audio_payload {
	uint32_t sequence;       /*!< Sequence number */
	/* Audio data follows immediately */
} __attribute__((packed));

/*! \brief State structure for ForkStream */
struct fork_stream_state {
	int sock;					/*!< UDP socket descriptor */
	struct sockaddr_in dest_addr;	/*!< Destination address structure */
	int framehook_id;			/*!< Framehook ID for detachment */
	uint32_t stream_id;			/*!< Unique stream identifier */
	uint32_t rx_sequence;		/*!< RX frame sequence counter */
	uint32_t tx_sequence;		/*!< TX frame sequence counter */
	char channel_id[64];		/*!< Channel identifier */
	char exten[32];				/*!< Extension number */
	char caller_id[32];			/*!< Caller ID */
	char called_id[32];			/*!< Called ID */
};

/* Forward declarations */
static void fork_stream_destroy_cb(void *data);
static uint32_t generate_stream_id(void);
static int send_signaling_packet(struct fork_stream_state *state, uint8_t direction);
static int send_audio_packet(struct fork_stream_state *state, uint8_t direction, 
	uint32_t sequence, const void *audio_data, uint16_t audio_length);
static int parse_arguments(const char *data, char *ip_addr, int *port, 
	struct fork_stream_state *state);

/*! \brief CLI command to enable ForkStream logging */
static char *handle_forkstream_log_on(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
		case CLI_INIT:
			e->command = "forkstream set logger on";
			e->usage = "Usage: forkstream set logger on\n"
				"       Enable detailed ForkStream logging\n";
			return NULL;
		case CLI_GENERATE:
			return NULL;
	}
	
	forkstream_logging_enabled = 1;
	ast_cli(a->fd, "ForkStream logging enabled\n");
	return CLI_SUCCESS;
}

/*! \brief CLI command to disable ForkStream logging */
static char *handle_forkstream_log_off(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
		case CLI_INIT:
			e->command = "forkstream set logger off";
			e->usage = "Usage: forkstream set logger off\n"
				"       Disable detailed ForkStream logging\n";
			return NULL;
		case CLI_GENERATE:
			return NULL;
	}
	
	forkstream_logging_enabled = 0;
	ast_cli(a->fd, "ForkStream logging disabled\n");
	return CLI_SUCCESS;
}

/*! \brief CLI command to show ForkStream logging status */
static char *handle_forkstream_log_status(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
		case CLI_INIT:
			e->command = "forkstream show logger";
			e->usage = "Usage: forkstream show logger\n"
				"       Show ForkStream logging status\n";
			return NULL;
		case CLI_GENERATE:
			return NULL;
	}
	
	ast_cli(a->fd, "ForkStream logging is %s\n", 
		forkstream_logging_enabled ? "enabled" : "disabled");
	return CLI_SUCCESS;
}

/*! \brief CLI command entries */
static struct ast_cli_entry forkstream_cli[] = {
	AST_CLI_DEFINE(handle_forkstream_log_on, "Enable ForkStream logging"),
	AST_CLI_DEFINE(handle_forkstream_log_off, "Disable ForkStream logging"),
	AST_CLI_DEFINE(handle_forkstream_log_status, "Show ForkStream logging status"),
};

/*! \brief Generate unique stream ID
 * \retval Unique 32-bit stream identifier
 */
static uint32_t generate_stream_id(void)
{
	static uint32_t counter = 0;
	uint32_t timestamp = (uint32_t)time(NULL);
	uint32_t random_part = (uint32_t)rand();
	
	/* Combine timestamp, random value and counter for uniqueness */
	return (timestamp & 0xFFFF0000) | ((random_part ^ (++counter)) & 0x0000FFFF);
}

/*! \brief Framehook callback function to handle audio frames
 * \param chan The channel the frame is being read from/written to
 * \param frame The frame being processed
 * \param direction The direction of the frame (read/write)
 * \param data Pointer to our fork_stream_state structure
 * \retval AST_FRAMEHOOK_INHERIT Keep the frame and continue processing
 */
static struct ast_frame *fork_stream_frame_cb(struct ast_channel *chan, struct ast_frame *frame, 
	enum ast_framehook_event event, void *data)
{
	struct fork_stream_state *state = data;
	uint8_t direction;
	uint32_t sequence;
	const char *direction_str;

	/* We only care about voice frames */
	if (!frame || frame->frametype != AST_FRAME_VOICE) {
		return frame;
	}

	/* Skip frames without data */
	if (!frame->data.ptr || frame->datalen <= 0) {
		return frame;
	}

	/* Determine direction and get sequence number */
	switch (event) {
		case AST_FRAMEHOOK_EVENT_READ:
			direction = DIRECTION_RX;
			direction_str = "RX";
			sequence = ++state->rx_sequence;
			break;
		case AST_FRAMEHOOK_EVENT_WRITE:
			direction = DIRECTION_TX;
			direction_str = "TX";
			sequence = ++state->tx_sequence;
			break;
		default:
			/* We only handle READ and WRITE events */
			return frame;
	}

	/* Send audio packet using TLV format */
	if (send_audio_packet(state, direction, sequence, frame->data.ptr, frame->datalen) == 0) {
		/* Log detailed info only if logging is enabled */
		if (forkstream_logging_enabled) {
			ast_verb(2, "ForkStream: Sent %s frame on channel %s: %d bytes (seq: %u)\n",
				direction_str, ast_channel_name(chan), frame->datalen, sequence);
		}
	}

	/* Always return the frame unchanged for normal processing */
	return frame;
}

/*! \brief Send signaling packet with channel metadata
 * \param state The fork stream state structure
 * \param direction Direction flag (DIRECTION_RX or DIRECTION_TX)
 * \retval 0 on success
 * \retval -1 on error
 */
static int send_signaling_packet(struct fork_stream_state *state, uint8_t direction)
{
	struct {
		struct forkstream_header header;
		struct signaling_payload payload;
	} __attribute__((packed)) packet;
	
	ssize_t sent_bytes;
	
	/* Setup header */
	packet.header.packet_type = PACKET_TYPE_SIGNALING;
	packet.header.packet_length = htons(sizeof(packet));
	packet.header.stream_id = htonl(state->stream_id);
	packet.header.direction = direction;
	
	/* Setup payload */
	memset(&packet.payload, 0, sizeof(packet.payload));
	strncpy(packet.payload.channel_id, state->channel_id, sizeof(packet.payload.channel_id) - 1);
	strncpy(packet.payload.exten, state->exten, sizeof(packet.payload.exten) - 1);
	strncpy(packet.payload.caller_id, state->caller_id, sizeof(packet.payload.caller_id) - 1);
	strncpy(packet.payload.called_id, state->called_id, sizeof(packet.payload.called_id) - 1);
	packet.payload.timestamp = htonl((uint32_t)time(NULL));
	
	/* Send packet */
	sent_bytes = sendto(state->sock, &packet, sizeof(packet), 0,
		(struct sockaddr *)&state->dest_addr, sizeof(state->dest_addr));
	
	if (sent_bytes != sizeof(packet)) {
		ast_log(LOG_WARNING, "ForkStream: Failed to send signaling packet (direction: %s): %s\n",
			direction == DIRECTION_RX ? "RX" : "TX", strerror(errno));
		return -1;
	}
	
	ast_verb(4, "ForkStream: Sent signaling packet (stream_id: %u, direction: %s)\n",
		state->stream_id, direction == DIRECTION_RX ? "RX" : "TX");
	
	return 0;
}

/*! \brief Send audio packet with frame data
 * \param state The fork stream state structure
 * \param direction Direction flag (DIRECTION_RX or DIRECTION_TX)
 * \param sequence Sequence number for this direction
 * \param audio_data Pointer to audio data
 * \param audio_length Length of audio data
 * \retval 0 on success
 * \retval -1 on error
 */
static int send_audio_packet(struct fork_stream_state *state, uint8_t direction,
	uint32_t sequence, const void *audio_data, uint16_t audio_length)
{
	struct {
		struct forkstream_header header;
		struct audio_payload payload;
	} __attribute__((packed)) packet_header;
	
	struct iovec iov[2];
	ssize_t sent_bytes;
	size_t total_size;
	
	/* Setup header */
	packet_header.header.packet_type = PACKET_TYPE_AUDIO;
	packet_header.header.packet_length = htons(sizeof(packet_header) + audio_length);
	packet_header.header.stream_id = htonl(state->stream_id);
	packet_header.header.direction = direction;
	
	/* Setup audio payload header */
	packet_header.payload.sequence = htonl(sequence);
	
	/* Setup scatter-gather for efficient sending */
	iov[0].iov_base = &packet_header;
	iov[0].iov_len = sizeof(packet_header);
	iov[1].iov_base = (void *)audio_data;
	iov[1].iov_len = audio_length;
	
	total_size = sizeof(packet_header) + audio_length;
	
	/* Send using sendmsg for efficient scatter-gather */
	struct msghdr msg = {0};
	msg.msg_name = &state->dest_addr;
	msg.msg_namelen = sizeof(state->dest_addr);
	msg.msg_iov = iov;
	msg.msg_iovlen = 2;
	
	sent_bytes = sendmsg(state->sock, &msg, 0);
	
	if (sent_bytes != (ssize_t)total_size) {
		ast_log(LOG_WARNING, "ForkStream: Failed to send audio packet (direction: %s, seq: %u): %s\n",
			direction == DIRECTION_RX ? "RX" : "TX", sequence, strerror(errno));
		return -1;
	}
	
	return 0;
}

/*! \brief Parse the ip:port argument and validate it
 * \param data The input string in format "ip:port"
 * \param ip_addr Output buffer for IP address (must be at least INET_ADDRSTRLEN)
 * \param port Output pointer for port number
 * \retval 0 on success
 * \retval -1 on error
 */
static int parse_destination(const char *data, char *ip_addr, int *port)
{
	char *parse_data;
	char *ip_part;
	char *port_part;
	char *endptr;
	long port_long;
	struct in_addr addr;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "ForkStream: No destination specified\n");
		return -1;
	}

	/* Make a copy of the data since strsep modifies the string */
	parse_data = ast_strdupa(data);

	/* Split the string by ':' */
	ip_part = strsep(&parse_data, ":");
	port_part = parse_data;

	if (ast_strlen_zero(ip_part) || ast_strlen_zero(port_part)) {
		ast_log(LOG_ERROR, "ForkStream: Invalid format. Expected 'ip:port', got '%s'\n", data);
		return -1;
	}

	/* Validate IP address */
	if (inet_aton(ip_part, &addr) == 0) {
		ast_log(LOG_ERROR, "ForkStream: Invalid IP address '%s'\n", ip_part);
		return -1;
	}

	/* Parse and validate port */
	port_long = strtol(port_part, &endptr, 10);
	if (*endptr != '\0') {
		ast_log(LOG_ERROR, "ForkStream: Port contains non-numeric characters '%s'\n", port_part);
		return -1;
	}

	if (port_long <= 0 || port_long > 65535) {
		ast_log(LOG_ERROR, "ForkStream: Port number %ld is out of valid range (1-65535)\n", port_long);
		return -1;
	}

	/* Copy results */
	ast_copy_string(ip_addr, ip_part, INET_ADDRSTRLEN);
	*port = (int)port_long;

	return 0;
}

/*! \brief Parse all ForkStream arguments
 * \param data The input string in format "ip:port[,channel_id][,exten][,caller_id][,called_id]"
 * \param ip_addr Output buffer for IP address (must be at least INET_ADDRSTRLEN)
 * \param port Output pointer for port number
 * \param state State structure to populate with optional parameters
 * \retval 0 on success
 * \retval -1 on error
 */
static int parse_arguments(const char *data, char *ip_addr, int *port, 
	struct fork_stream_state *state)
{
	char *parse_data;
	char *token;
	int arg_count = 0;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "ForkStream: No arguments specified\n");
		return -1;
	}

	/* Make a copy of the data since strsep modifies the string */
	parse_data = ast_strdupa(data);

	/* Initialize optional fields to empty */
	memset(state->channel_id, 0, sizeof(state->channel_id));
	memset(state->exten, 0, sizeof(state->exten));
	memset(state->caller_id, 0, sizeof(state->caller_id));
	memset(state->called_id, 0, sizeof(state->called_id));

	/* Parse comma-separated arguments */
	while ((token = strsep(&parse_data, ",")) != NULL) {
		/* Skip empty tokens */
		if (ast_strlen_zero(token)) {
			continue;
		}

		switch (arg_count) {
			case 0:
				/* First argument: ip:port (required) */
				if (parse_destination(token, ip_addr, port) != 0) {
					return -1;
				}
				break;
			case 1:
				/* Second argument: channel_id (optional) */
				ast_copy_string(state->channel_id, token, sizeof(state->channel_id));
				break;
			case 2:
				/* Third argument: exten (optional) */
				ast_copy_string(state->exten, token, sizeof(state->exten));
				break;
			case 3:
				/* Fourth argument: caller_id (optional) */
				ast_copy_string(state->caller_id, token, sizeof(state->caller_id));
				break;
			case 4:
				/* Fifth argument: called_id (optional) */
				ast_copy_string(state->called_id, token, sizeof(state->called_id));
				break;
			default:
				ast_log(LOG_WARNING, "ForkStream: Ignoring extra argument: '%s'\n", token);
				break;
		}
		arg_count++;
	}

	/* If channel_id wasn't provided, we'll set it later from the actual channel */
	if (ast_strlen_zero(state->channel_id)) {
		ast_copy_string(state->channel_id, "unknown", sizeof(state->channel_id));
	}

	ast_verb(3, "ForkStream: Parsed arguments - IP: %s, Port: %d, Channel: %s, Exten: %s, Caller: %s, Called: %s\n",
		ip_addr, *port, state->channel_id, state->exten, state->caller_id, state->called_id);

	return 0;
}

/*! \brief Create and configure UDP socket
 * \param state The fork stream state structure to populate
 * \param ip_addr The destination IP address
 * \param port The destination port
 * \retval 0 on success
 * \retval -1 on error
 */
static int create_udp_socket(struct fork_stream_state *state, const char *ip_addr, int port)
{
	int sock;
	struct sockaddr_in *dest_addr;

	/* Create UDP socket */
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) {
		ast_log(LOG_ERROR, "ForkStream: Failed to create UDP socket: %s\n", strerror(errno));
		return -1;
	}

	/* Setup destination address */
	dest_addr = &state->dest_addr;
	memset(dest_addr, 0, sizeof(*dest_addr));
	dest_addr->sin_family = AF_INET;
	dest_addr->sin_port = htons(port);

	if (inet_aton(ip_addr, &dest_addr->sin_addr) == 0) {
		ast_log(LOG_ERROR, "ForkStream: Failed to convert IP address '%s'\n", ip_addr);
		close(sock);
		return -1;
	}

	state->sock = sock;

	ast_verb(3, "ForkStream: Created UDP socket, destination %s:%d\n", ip_addr, port);
	return 0;
}

/*! \brief Create and attach framehook to the channel
 * \param chan The channel to attach the framehook to
 * \param state The fork stream state structure
 * \retval 0 on success
 * \retval -1 on error
 */
static int attach_framehook(struct ast_channel *chan, struct fork_stream_state *state)
{
	struct ast_framehook_interface framehook_interface;
	int framehook_id;

	/* Setup framehook interface */
	memset(&framehook_interface, 0, sizeof(framehook_interface));
	framehook_interface.version = AST_FRAMEHOOK_INTERFACE_VERSION;
	framehook_interface.event_cb = fork_stream_frame_cb;
	framehook_interface.destroy_cb = fork_stream_destroy_cb;
	framehook_interface.data = state;
	framehook_interface.disable_inheritance = 0;  /* Allow inheritance to bridged channels */

	/* Attach framehook to capture both read and write frames */
	framehook_id = ast_framehook_attach(chan, &framehook_interface);
	if (framehook_id < 0) {
		ast_log(LOG_ERROR, "ForkStream: Failed to attach framehook to channel %s\n",
			ast_channel_name(chan));
		return -1;
	}

	/* Store framehook ID in state for later cleanup */
	state->framehook_id = framehook_id;

	ast_verb(3, "ForkStream: Attached framehook (ID: %d) to channel %s\n",
		framehook_id, ast_channel_name(chan));
	return 0;
}

/*! \brief Destroy callback to cleanup resources when framehook is destroyed
 * \param data Pointer to our fork_stream_state structure
 */
static void fork_stream_destroy_cb(void *data)
{
	struct fork_stream_state *state = data;

	if (!state) {
		ast_log(LOG_WARNING, "ForkStream: Destroy callback called with NULL state\n");
		return;
	}

	ast_verb(3, "ForkStream: Cleaning up resources for framehook ID %d\n", state->framehook_id);

	/* No need to detach framehook here - this callback is called when it's being destroyed */

	/* Close UDP socket */
	if (state->sock >= 0) {
		close(state->sock);
		ast_verb(4, "ForkStream: Closed UDP socket (framehook ID: %d)\n", state->framehook_id);
		state->sock = -1;
	}

	/* Free the state structure */
	ast_free(state);
	ast_verb(3, "ForkStream: Resource cleanup completed (framehook ID: %d)\n", state->framehook_id);
}



/*! \brief Main application function */
static int fork_stream_exec(struct ast_channel *chan, const char *data)
{
	struct fork_stream_state *state;
	char ip_addr[INET_ADDRSTRLEN];
	int port;

	ast_verb(2, "ForkStream called on channel %s with data: %s\n", 
		ast_channel_name(chan), data);

	/* Allocate state structure */
	state = ast_calloc(1, sizeof(*state));
	if (!state) {
		ast_log(LOG_ERROR, "ForkStream: Failed to allocate state structure\n");
		return -1;
	}

	/* Initialize state fields */
	state->sock = -1;
	state->framehook_id = -1;
	state->rx_sequence = 0;
	state->tx_sequence = 0;

	/* Parse and validate arguments */
	if (parse_arguments(data, ip_addr, &port, state)) {
		ast_log(LOG_ERROR, "ForkStream: Failed to parse arguments\n");
		ast_free(state);
		return -1;
	}

	/* If channel_id wasn't specified, use the actual channel name */
	if (ast_strlen_zero(state->channel_id) || !strcmp(state->channel_id, "unknown")) {
		ast_copy_string(state->channel_id, ast_channel_name(chan), sizeof(state->channel_id));
	}

	/* Generate unique stream ID */
	state->stream_id = generate_stream_id();

	/* Create UDP socket */
	if (create_udp_socket(state, ip_addr, port)) {
		ast_log(LOG_ERROR, "ForkStream: Failed to create UDP socket\n");
		ast_free(state);
		return -1;
	}

	/* Attach framehook to capture audio frames */
	if (attach_framehook(chan, state)) {
		ast_log(LOG_ERROR, "ForkStream: Failed to attach framehook\n");
		close(state->sock);
		ast_free(state);
		return -1;
	}

	/* Send initial signaling packets for both directions */
	if (send_signaling_packet(state, DIRECTION_RX) != 0) {
		ast_log(LOG_WARNING, "ForkStream: Failed to send RX signaling packet\n");
	}
	
	if (send_signaling_packet(state, DIRECTION_TX) != 0) {
		ast_log(LOG_WARNING, "ForkStream: Failed to send TX signaling packet\n");
	}

	ast_verb(2, "ForkStream: Successfully initialized for %s:%d on channel %s (stream_id: %u)\n",
		ip_addr, port, ast_channel_name(chan), state->stream_id);

	/* 
	 * Note: We don't free the state structure here because it will be freed
	 * automatically by the destroy callback when the framehook is destroyed.
	 * This happens when the channel is terminated, ensuring proper cleanup of all resources.
	 */

	return 0;
}

/*! \brief Load module function */
static int load_module(void)
{
	int res;

	/* Register CLI commands */
	ast_cli_register_multiple(forkstream_cli, ARRAY_LEN(forkstream_cli));

	/* Register the application */
	res = ast_register_application_xml(app, fork_stream_exec);
	if (res) {
		ast_log(LOG_ERROR, "Failed to register ForkStream application\n");
		ast_cli_unregister_multiple(forkstream_cli, ARRAY_LEN(forkstream_cli));
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_verb(2, "ForkStream application loaded successfully\n");
	return AST_MODULE_LOAD_SUCCESS;
}

/*! \brief Unload module function */
static int unload_module(void)
{
	int res;

	/* Unregister CLI commands */
	ast_cli_unregister_multiple(forkstream_cli, ARRAY_LEN(forkstream_cli));

	/* Unregister the application first to prevent new instances */
	res = ast_unregister_application(app);
	if (res) {
		ast_log(LOG_WARNING, "Failed to unregister ForkStream application\n");
	}

	/* 
	 * Note: Any active ForkStream instances will be automatically cleaned up
	 * by their hangup handlers when the channels are terminated.
	 * We don't need to explicitly track and clean up active instances
	 * because Asterisk handles this through the hangup handler mechanism.
	 */

	ast_verb(2, "ForkStream application unloaded\n");
	return res;
}

/*! \brief Reload module function */
static int reload_module(void)
{
	/* 
	 * For this module, reload is the same as unload + load
	 * since we don't have any persistent configuration to reload.
	 * Active ForkStream instances will continue running unaffected.
	 */
	ast_verb(2, "ForkStream module reload requested\n");
	
	/* No configuration to reload, so just return success */
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Fork Stream Application",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.reload = reload_module,
	.load_pri = AST_MODPRI_DEFAULT,
); 