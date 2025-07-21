/*
 * Simple test program for ForkStream argument parsing
 * 
 * This program tests the parse_destination function with various inputs
 * to ensure proper validation and error handling.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#define INET_ADDRSTRLEN 16

/* Simplified versions of Asterisk functions for testing */
#define ast_strlen_zero(str) (!str || str[0] == '\0')
#define ast_log(level, fmt, ...) printf("[%s] " fmt, #level, ##__VA_ARGS__)
#define LOG_ERROR "ERROR"

static inline void ast_copy_string(char *dst, const char *src, size_t size)
{
	strncpy(dst, src, size - 1);
	dst[size - 1] = '\0';
}

static inline char *ast_strdupa(const char *str)
{
	return strdup(str);  /* Note: This leaks memory in the test, but mimics Asterisk behavior */
}

/* Copy of the parse_destination function from our module */
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

/* Test cases */
static void test_case(const char *input, int expected_result)
{
	char ip_addr[INET_ADDRSTRLEN];
	int port;
	int result;

	printf("\n=== Testing: '%s' ===\n", input ? input : "(NULL)");
	
	result = parse_destination(input, ip_addr, &port);
	
	if (result == expected_result) {
		if (result == 0) {
			printf("✅ SUCCESS: Parsed to IP=%s, Port=%d\n", ip_addr, port);
		} else {
			printf("✅ SUCCESS: Correctly rejected invalid input\n");
		}
	} else {
		printf("❌ FAILED: Expected %d, got %d\n", expected_result, result);
	}
}

int main(void)
{
	printf("ForkStream Argument Parsing Test\n");
	printf("================================\n");

	/* Valid cases */
	test_case("192.168.1.100:8080", 0);
	test_case("127.0.0.1:1234", 0);
	test_case("10.0.0.1:65535", 0);
	test_case("172.16.0.1:1", 0);

	/* Invalid cases */
	test_case(NULL, -1);
	test_case("", -1);
	test_case("192.168.1.100", -1);  /* Missing port */
	test_case(":8080", -1);           /* Missing IP */
	test_case("192.168.1.100:", -1);  /* Missing port after : */
	test_case("invalid.ip:8080", -1); /* Invalid IP */
	test_case("192.168.1.100:abc", -1); /* Non-numeric port */
	test_case("192.168.1.100:0", -1);   /* Port out of range */
	test_case("192.168.1.100:65536", -1); /* Port out of range */
	test_case("192.168.1.100:-1", -1);  /* Negative port */

	printf("\n=== Test Complete ===\n");
	return 0;
} 