#define _DEFAULT_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define RESPONSE_TIMEOUT_MS 3000

static speed_t baud_constant(long baud)
{
	switch (baud) {
	case 1200: return B1200;
	case 2400: return B2400;
	case 4800: return B4800;
	case 9600: return B9600;
	case 19200: return B19200;
	case 38400: return B38400;
	case 57600: return B57600;
	case 115200: return B115200;
#ifdef B230400
	case 230400: return B230400;
#endif
	default: return (speed_t)0;
	}
}

static int wait_for_fd(int fd, short events)
{
	struct pollfd descriptor = { .fd = fd, .events = events };
	int result;

	do {
		result = poll(&descriptor, 1, RESPONSE_TIMEOUT_MS);
	} while (result < 0 && errno == EINTR);

	if (result == 0) {
		errno = ETIMEDOUT;
		return -1;
	}
	if (result < 0)
		return -1;
	if (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) {
		errno = EIO;
		return -1;
	}
	if (!(descriptor.revents & events)) {
		errno = EIO;
		return -1;
	}

	return 0;
}

static int write_all(int fd, const unsigned char *buffer, size_t length)
{
	size_t sent = 0;

	while (sent < length) {
		ssize_t result = write(fd, buffer + sent, length - sent);
		if (result > 0) {
			sent += (size_t)result;
			continue;
		}
		if (result < 0 && errno == EINTR)
			continue;
		if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
			if (wait_for_fd(fd, POLLOUT) == 0)
				continue;
		}
		return -1;
	}

	return 0;
}

static int read_exact(int fd, unsigned char *buffer, size_t length)
{
	size_t received = 0;

	while (received < length) {
		if (wait_for_fd(fd, POLLIN) < 0)
			return -1;

		ssize_t result = read(fd, buffer + received, length - received);
		if (result > 0) {
			received += (size_t)result;
			continue;
		}
		if (result < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
			continue;
		if (result == 0)
			errno = EIO;
		return -1;
	}

	return 0;
}

static void print_usage(const char *program)
{
	fprintf(stderr, "Usage: %s [-d /dev/ttyACM0] [-b baud] [-n count]\n", program);
	fprintf(stderr, "  -n count  capture count consecutive samples and report their mean\n");
	fprintf(stderr, "Supported baud rates: 1200, 2400, 4800, 9600, 19200, ");
	fprintf(stderr, "38400, 57600, 115200, 230400 (when available)\n");
}

static uint16_t read_u16_be(const unsigned char *bytes)
{
	return (uint16_t)(((uint16_t)bytes[0] << 8) | (uint16_t)bytes[1]);
}

int main(int argc, char **argv)
{
	const char *device = "/dev/ttyACM0";
	long baud = 115200;
	unsigned long sample_count = 1;
	int option;
	int serial_fd;
	struct termios serial_settings;
	unsigned char sampling_command = 's';
	unsigned char measurement_command = 0x04;
	unsigned char exit_sampling_command[6] = { 0, 0, 0, 0, 0, 0 };
	unsigned char mode_response[3];
	unsigned char measurement_response[8];
	char confirmation[16];
	uint16_t a, b, c;
	uint16_t delta_ab, delta_bc;
	double frequency_hz;
	double mean_frequency_hz = 0.0;
	int output_failed;

	while ((option = getopt(argc, argv, "d:b:n:h")) != -1) {
		switch (option) {
		case 'd':
			device = optarg;
			break;
		case 'b': {
			char *end = NULL;
			errno = 0;
			baud = strtol(optarg, &end, 10);
			if (errno != 0 || end == optarg || *end != '\0' ||
				baud_constant(baud) == (speed_t)0) {
				fprintf(stderr, "Unsupported baud rate: %s\n", optarg);
				print_usage(argv[0]);
				return EXIT_FAILURE;
			}
			break;
		}
		case 'n': {
			char *end = NULL;
			errno = 0;
			sample_count = strtoul(optarg, &end, 10);
			if (errno != 0 || end == optarg || *optarg == '-' || *end != '\0' ||
				sample_count == 0) {
				fprintf(stderr, "Invalid sample count: %s\n", optarg);
				print_usage(argv[0]);
				return EXIT_FAILURE;
			}
			break;
		}
		case 'h':
			print_usage(argv[0]);
			return EXIT_SUCCESS;
		default:
			print_usage(argv[0]);
			return EXIT_FAILURE;
		}
	}

	if (optind != argc) {
		print_usage(argv[0]);
		return EXIT_FAILURE;
	}

	serial_fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (serial_fd < 0) {
		perror(device);
		return EXIT_FAILURE;
	}

	if (tcgetattr(serial_fd, &serial_settings) < 0) {
		perror("tcgetattr");
		close(serial_fd);
		return EXIT_FAILURE;
	}

	cfmakeraw(&serial_settings);
	serial_settings.c_cflag &= ~(tcflag_t)(PARENB | CSTOPB | CSIZE);
	serial_settings.c_cflag |= CS8 | CLOCAL | CREAD;
#ifdef CRTSCTS
	serial_settings.c_cflag &= ~CRTSCTS;
#endif
	serial_settings.c_iflag &= ~(tcflag_t)(IXON | IXOFF | IXANY);
	if (cfsetispeed(&serial_settings, baud_constant(baud)) < 0 ||
		cfsetospeed(&serial_settings, baud_constant(baud)) < 0 ||
		tcsetattr(serial_fd, TCSANOW, &serial_settings) < 0) {
		perror("configuring serial port");
		close(serial_fd);
		return EXIT_FAILURE;
	}

	if (write_all(serial_fd, &sampling_command, sizeof(sampling_command)) < 0) {
		perror("sending sampling command");
		close(serial_fd);
		return EXIT_FAILURE;
	}

	if (read_exact(serial_fd, mode_response, sizeof(mode_response)) < 0) {
		perror("waiting for S01 sampling-mode response");
		close(serial_fd);
		return EXIT_FAILURE;
	}
	if (memcmp(mode_response, "S01", sizeof(mode_response)) != 0) {
		fprintf(stderr, "Unexpected sampling-mode response: %02x %02x %02x\n",
			mode_response[0], mode_response[1], mode_response[2]);
		close(serial_fd);
		return EXIT_FAILURE;
	}

	fprintf(stderr, "Sampling mode ready.\n");

	for (unsigned long sample_index = 0; ; sample_index++) {
		fprintf(stderr, "Sample %lu/%lu: press a remote button, then press Enter to capture. ",
			sample_index + 1, sample_count);
		fflush(stderr);
		if (fgets(confirmation, sizeof(confirmation), stdin) == NULL) {
			fprintf(stderr, "\nMeasurement cancelled.\n");
			if (write_all(serial_fd, exit_sampling_command, sizeof(exit_sampling_command)) < 0)
				perror("sending sampling-mode exit command");
			close(serial_fd);
			return EXIT_FAILURE;
		}

		if (tcflush(serial_fd, TCIFLUSH) < 0) {
			perror("clearing serial input buffer");
			if (write_all(serial_fd, exit_sampling_command, sizeof(exit_sampling_command)) < 0)
				perror("sending sampling-mode exit command");
			close(serial_fd);
			return EXIT_FAILURE;
		}

		if (write_all(serial_fd, &measurement_command, sizeof(measurement_command)) < 0) {
			perror("sending measurement command");
			close(serial_fd);
			return EXIT_FAILURE;
		}
		if (read_exact(serial_fd, measurement_response, sizeof(measurement_response)) < 0) {
			perror("waiting for 8-byte measurement response");
			close(serial_fd);
			return EXIT_FAILURE;
		}

		printf("Response bytes:");
		for (size_t index = 0; index < sizeof(measurement_response); index++)
			printf(" %02x", measurement_response[index]);
		putchar('\n');

		a = read_u16_be(&measurement_response[0]);
		b = read_u16_be(&measurement_response[2]);
		c = read_u16_be(&measurement_response[4]);
		if (!(a < b && b < c)) {
			fprintf(stderr, "Invalid sample %lu: expected A < B < C (A=%u, B=%u, C=%u).\n",
				sample_index + 1, (unsigned)a, (unsigned)b, (unsigned)c);
			if (write_all(serial_fd, exit_sampling_command, sizeof(exit_sampling_command)) < 0)
				perror("sending sampling-mode exit command");
			close(serial_fd);
			return EXIT_FAILURE;
		}

		delta_ab = (uint16_t)(b - a);
		delta_bc = (uint16_t)(c - b);
		frequency_hz = 12000000.0 / (((double)delta_ab + (double)delta_bc) / 2.0);
		mean_frequency_hz += (frequency_hz - mean_frequency_hz) /
			(double)(sample_index + 1);

		if (sample_index + 1 == sample_count)
			break;
	}

	if (sample_count == 1)
		printf("Frequency: %.3f Hz\n", mean_frequency_hz);
	else
		printf("Mean frequency (%lu samples): %.3f Hz\n", sample_count, mean_frequency_hz);
	output_failed = fflush(stdout) == EOF;
	if (write_all(serial_fd, exit_sampling_command, sizeof(exit_sampling_command)) < 0) {
		perror("sending sampling-mode exit command");
		close(serial_fd);
		return EXIT_FAILURE;
	}
	close(serial_fd);
	if (output_failed) {
		fprintf(stderr, "Failed to flush frequency output.\n");
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
